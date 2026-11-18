#!/usr/bin/env python3
"""Lockstep client for an outside AI player (BACKLOG B-306).

Runs a copy of a project with the UZUTelemetry and UZUControl autoloads
(ai_loop/telemetry/telemetry.gd, ai_loop/selfplay/control.gd) and drives it
step by step: the game only advances when step() is called, and every step
returns the observation (frame, telemetry events, requested state, frame image).

    from aiplayer import Game
    with Game("ai_loop/selfplay/sample_game", seed=1) as game:
        obs = game.observe(state=["Main/Player:position"])
        while not obs["done"]:
            obs = game.step(press=["move_right"])

The project itself is not changed. Standard library only.
Usage: ai_loop/aiplayer.py --self-test
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uzu  # noqa: E402  Finds the editor binary for this OS and sets up rendering (B-801).
PREFIX = "UZU_STEP "
AUTOLOADS = 'UZUControl="*res://addons/uzu_selfplay/control.gd"\nUZUTelemetry="*res://addons/uzu_selfplay/telemetry.gd"\nUZUStateHash="*res://addons/uzu_selfplay/state_hash.gd"\n'


class Game:
    def __init__(self, project, seed=None, render=False, telemetry_log=None):
        self.work = tempfile.mkdtemp(prefix="uzu_aiplayer_")
        game = os.path.join(self.work, "game")
        shutil.copytree(project, game, ignore=shutil.ignore_patterns(".godot"))
        addon = os.path.join(game, "addons", "uzu_selfplay")
        os.makedirs(addon, exist_ok=True)
        shutil.copy(os.path.join(HERE, "selfplay", "control.gd"), addon)
        shutil.copy(os.path.join(HERE, "telemetry", "telemetry.gd"), addon)
        shutil.copy(os.path.join(HERE, "selfplay", "state_hash.gd"), addon)  # State hashes and save states (B-307, B-309).
        cfg = os.path.join(game, "project.godot")
        with open(cfg, encoding="utf-8") as f:
            text = f.read()
        text = text.replace("\n[autoload]\n", "\n[autoload]\n\n" + AUTOLOADS, 1) if "\n[autoload]\n" in text else text + "\n[autoload]\n\n" + AUTOLOADS
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(text)
        # B-311: the copy has no .godot, so imported resources (fonts,
        # textures) have no loaders and scripts that preload them fail to
        # parse. Import the copy once before running it.
        if not os.path.exists(os.path.join(game, ".godot", "imported")):
            subprocess.run([uzu.find_godot(), "--headless", "--path", game, "--import"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        user_args = ["--uzu-telemetry", telemetry_log or os.path.join(self.work, "events.jsonl")]
        if seed is not None:
            user_args += ["--uzu-seed", str(seed)]  # control.gd fixes the global random seed (B-305).
        cmd = [uzu.find_godot(), "--path", game, "--fixed-fps", "60"]
        env = None
        if render:
            cmd, env = uzu.render_command(cmd + uzu.driver() + ["--resolution", "640x360"], "640x360")
        else:
            cmd.insert(1, "--headless")
        self.proc = subprocess.Popen(cmd + ["--"] + user_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1, env=env)
        self.seed = seed
        self.last = self._read()

    def _read(self):
        for line in self.proc.stdout:
            if line.startswith(PREFIX):
                return json.loads(line[len(PREFIX):])
        raise RuntimeError(f"the game ended (exit code {self.proc.wait()})")

    def _send(self, command):
        if self.last.get("ended"):
            raise RuntimeError("the game has ended (it quit by itself); start a new Game")
        self.proc.stdin.write(json.dumps(command) + "\n")
        self.proc.stdin.flush()
        self.last = self._read()
        return self.last

    def observe(self, state=None, image=None):
        """The last observation; with state or image, advances one frame to read them."""
        if state is None and image is None:
            return self.last
        return self.step(frames=1, state=state, image=image)

    def step(self, frames=1, press=(), release=(), state=None, image=None):
        command = {"frames": frames, "press": list(press), "release": list(release)}
        if state is not None:
            command["state"] = list(state)
        if image:
            command["image"] = os.path.abspath(image)
        return self._send(command)

    def save(self, name="default"):
        """Snapshots the declared game state into a slot (B-309); the game advances one frame."""
        return self._send({"save": name, "frames": 1})

    def restore(self, name="default"):
        """Puts a saved state back, with the actions that were pressed then (B-309)."""
        return self._send({"restore": name, "frames": 1})

    def state_hash(self):
        """The hash of the declared game state (B-307) after one more frame."""
        return self._send({"hash": True, "frames": 1})["state_hash"]

    def reset(self):
        return self._send({"reset": True})

    def close(self):
        if self.proc.poll() is None:
            try:
                self.proc.stdin.write(json.dumps({"quit": True}) + "\n")
                self.proc.stdin.flush()
            except BrokenPipeError:
                pass
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        shutil.rmtree(self.work, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def _play(game, policy, max_steps=2000):
    """Runs a policy(x) -> list of actions to press this frame; returns (cleared, deaths, checkpoints, steps)."""
    obs = game.step(state=["Main/Player:position"])
    deaths, checkpoints = 0, []
    for steps in range(max_steps):
        for e in obs["events"]:
            deaths += e["type"] == "death"
            if e["type"] == "checkpoint":
                checkpoints.append(e["id"])
        if obs["done"] or obs["ended"]:
            return obs["done"], deaths, checkpoints, steps
        x = obs["state"]["Main/Player:position"][0]
        jump = policy(x)
        obs = game.step(press=["move_right"] + (["jump"] if jump else []), release=[] if jump else ["jump"])
    return False, deaths, checkpoints, max_steps


def self_test():
    pits = [300, 700]
    sample = os.path.join(HERE, "selfplay", "sample_game")
    with Game(sample) as game:
        assert game.last["frame"] >= 0 and game.last["events"][0]["type"] == "start", game.last
        # A policy that reads the player's position and jumps just before each pit.
        cleared, deaths, checkpoints, steps = _play(game, lambda x: any(p - 16 <= x < p - 8 for p in pits))
        assert cleared and deaths == 0 and checkpoints == ["cp1", "cp2", "cp3"], (cleared, deaths, checkpoints)
        assert game.last["ended"], "the sample game quits on the clear; the last observation says so"
        try:
            game.step()
            raise AssertionError("step after the end must fail")
        except RuntimeError:
            pass
    with Game(sample, seed=3) as game:
        game.step(frames=30, press=["move_right"])
        # Reset starts the scene again from the beginning.
        obs = game.reset()
        assert obs["reset"] and not obs["done"], obs
        obs = game.step(state=["Main/Player:position", "Main:respawn_x", "Nope:x"])
        assert obs["state"]["Main/Player:position"] == [0.0, 0.0] and obs["state"]["Main:respawn_x"] == 0.0, obs
        assert "error" in obs["state"]["Nope:x"], obs
        # A policy that never jumps dies at the first pit, and the deaths arrive in the observations.
        cleared, deaths, checkpoints, _ = _play(game, lambda x: False, max_steps=200)
        assert not cleared and deaths >= 2 and checkpoints == ["cp1"], (cleared, deaths, checkpoints)
        # Save states (B-309): play on from a save, then restore and play the same
        # input again: the state hashes match, and the player is back where it was.
        game.reset()
        game.step(frames=100, press=["move_right"], state=["Main/Player:position"])
        at_save = game.save("before_pit")["state"]["Main/Player:position"]
        plan = [(60, []), (1, ["jump"]), (1, []), (90, [])]  # Jump over the first pit, run on.
        def play_plan():
            for frames, press in plan:
                game.step(frames=frames, press=press, release=[] if press else ["jump"])
            return game.state_hash()
        first = play_plan()
        restored = game.restore("before_pit")
        assert restored.get("restored") == "before_pit" and restored["restored_nodes"] >= 2, restored
        assert game.step(state=["Main/Player:position"])["state"]["Main/Player:position"][0] == at_save[0] + 4.0, "back at the save (one frame on)"
        game.restore("before_pit")
        second = play_plan()
        assert first == second, (first, second)
        # A different input from the same save gives a different state (the hash is not trivial).
        game.restore("before_pit")
        game.step(frames=152)
        assert game.state_hash() != first
        assert game.step(press=["jump"]).get("error") is None
        assert "error" in game._send({"restore": "nope", "frames": 1})
        # Several frames per step.
        before = game.step(frames=1)["frame"]
        assert game.step(frames=10)["frame"] == before + 10
    print("aiplayer selftest: ok")
    return 0


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
