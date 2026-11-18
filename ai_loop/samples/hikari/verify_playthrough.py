#!/usr/bin/env python3
"""B-1002: closed-loop AI-player verification of the sample game
'hikari no kakera' (灯りの欠片).

Drives the real game through ai_loop/aiplayer.py (B-306 lockstep control):
title -> walk to each shard -> collect -> light the lantern -> clear.
The judgement is mechanical, not visual:

  * telemetry checkpoints arrive in order: start, shard_1, shard_2,
    shard_3, lantern_lit
  * the declared state (B-307, Main.uzu_state) ends at shards=3,
    cleared=true
  * the state hash at game start is identical across two runs with the
    same seed (determinism probe)

Usage: python3 verify_playthrough.py [report.json]
Exit code 0 = all checks passed.
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from aiplayer import Game  # noqa: E402

STATE = ["Main/Player:global_position", "Main:shards", "Main:cleared"]
# (name, (x, z) to stand, shards expected after interacting there)
ROUTE = [
    ("shard_crate", (6.1, 0.6), 1),
    ("shard_pond", (7.6, 6.4), 2),
    ("shard_tree", (-4.9, -8.6), 3),
    ("lantern", (0.0, -8.0), 3),
]
EXPECTED_EVENTS = ["start", "shard_1", "shard_2", "shard_3", "lantern_lit"]


def event_names(events):
    names = []
    for e in events:
        if isinstance(e, dict):
            for key in ("name", "checkpoint", "event"):
                if key in e and isinstance(e[key], str):
                    names.append(e[key])
                    break
            else:
                if e.get("type") == "checkpoint" and "data" in e:
                    names.append(str(e["data"]))
    return names


def pos_of(obs):
    v = obs.get("state", {}).get("Main/Player:global_position")
    if isinstance(v, (list, tuple)) and len(v) == 3:
        return (float(v[0]), float(v[2]))
    return None


def walk_to(game, target, max_steps=500):
    tx, tz = target
    held = set()
    obs = game.observe(state=STATE)
    for _ in range(max_steps):
        p = pos_of(obs)
        if p is not None:
            dx, dz = tx - p[0], tz - p[1]
            if math.hypot(dx, dz) < 1.0:
                if held:
                    obs = game.step(frames=1, release=sorted(held), state=STATE)
                return obs
            want = set()
            if abs(dx) > 0.25:
                want.add("move_right" if dx > 0 else "move_left")
            if abs(dz) > 0.25:
                want.add("move_down" if dz > 0 else "move_up")
            obs = game.step(
                frames=6,
                press=sorted(want - held),
                release=sorted(held - want),
                state=STATE,
            )
            held = want
        else:
            obs = game.step(frames=6, state=STATE)
    raise RuntimeError("walk_to timed out heading for %s" % (target,))


def interact(game):
    game.step(frames=2, press=["interact"])
    return game.step(frames=10, release=["interact"], state=STATE)


def playthrough(seed):
    seen = []
    hashes = {}
    final = {}
    with Game(HERE, seed=seed) as game:
        game.observe(state=STATE)
        obs = game.step(frames=30, press=["interact"], state=STATE)
        game.step(frames=5, release=["interact"], state=STATE)
        seen += event_names(obs.get("events", []))
        hashes["start"] = game.state_hash()
        for name, target, want_shards in ROUTE:
            obs = walk_to(game, target)
            seen += event_names(obs.get("events", []))
            for _try in range(4):
                obs = interact(game)
                seen += event_names(obs.get("events", []))
                if name == "lantern":
                    if obs.get("state", {}).get("Main:cleared"):
                        break
                elif obs.get("state", {}).get("Main:shards") == want_shards:
                    break
            hashes[name] = game.state_hash()
            final = obs.get("state", {})
    return {"events": seen, "hashes": hashes, "final_state": final}


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "verify_report.json")
    runs = [playthrough(7), playthrough(7)]
    checks = []
    for i, run in enumerate(runs):
        order = [e for e in EXPECTED_EVENTS if e in run["events"]]
        checks.append({
            "run": i + 1,
            "checkpoints_in_order": order == EXPECTED_EVENTS and
                [run["events"].index(e) for e in EXPECTED_EVENTS] ==
                sorted(run["events"].index(e) for e in EXPECTED_EVENTS),
            "final_shards": run["final_state"].get("Main:shards"),
            "final_cleared": run["final_state"].get("Main:cleared"),
        })
    report = {
        "game": "hikari no kakera (ai_loop/samples/hikari)",
        "tool": "ai_loop/aiplayer.py (B-306) + state hash (B-307)",
        "runs": runs,
        "checks": checks,
        "start_hash_deterministic": runs[0]["hashes"].get("start") == runs[1]["hashes"].get("start"),
    }
    report["passed"] = all(
        c["checkpoints_in_order"] and c["final_shards"] == 3 and c["final_cleared"] is True
        for c in checks
    ) and report["start_hash_deterministic"]
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)
    print("UZU_RESULT " + json.dumps({
        "passed": report["passed"],
        "checks": checks,
        "start_hash_deterministic": report["start_hash_deterministic"],
        "report": out_path,
    }, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
