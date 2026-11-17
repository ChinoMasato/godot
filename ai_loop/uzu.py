#!/usr/bin/env python3
"""U.ZU Engine agent tools: one cross-platform entry (BACKLOG B-801).

Runs on Linux, Windows and macOS with Python 3.8+ and the standard library.
The shell scripts (engine.sh, look.sh, selfplay.sh, screenshot.sh, publish.sh)
are thin wrappers around this file; on Windows call it directly:
    py ai_loop\\uzu.py engine my_game tree --scene res://main.tscn
or through ai_loop\\windows\\uzu.bat.

  uzu.py engine   <project> <command> [options]   scene, class and project operations (B-401, B-201..B-203, B-304)
  uzu.py look     <project> <command> [options]   the U.ZU look (B-7xx), shot / check / panorama
  uzu.py selfplay <project> --input replay.json [--frames 1800] [--seed N] [--out r.json] [--movie m.avi] [--log e.jsonl]
  uzu.py publish  <build | project> [options]     through the bridge API (B-505)
  uzu.py selftest [engine | selfplay | tools | all]

engine commands: run, check, validate, shot, and every command of ai_loop/engine_tool.gd
(tree, props, get, set, add, instance, remove, reparent, call, connect, script, new,
classes, describe, actions). look commands: shot, check, panorama, and those of
ai_loop/look_tool.gd (apply, rig, lamp, camera, material, info).
Every command prints one JSON line {"ok": bool, ...}; exit 0 on success.

Environment:
  GODOT                 editor binary (default: found in bin/ for this OS; on Windows the
                        .console.exe is preferred, since it keeps stdout/stdin)
  UZU_SOFTWARE_RENDER   1 forces software Vulkan on a virtual display (Linux; the default
                        when there is no DISPLAY). Elsewhere the real GPU is used.
  UZU_RENDERING_DRIVER  rendering driver for shots and movies (default: vulkan)
"""

import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LAVAPIPE = "/usr/share/vulkan/icd.d/lvp_icd.json"


class ToolError(Exception):
    def __init__(self, message, code=1, **extra):
        super().__init__(message)
        self.message, self.code, self.extra = message, code, extra


# ---------------------------------------------------------------- helpers


def find_godot():
    """The editor binary: $GODOT, else the newest editor build in bin/ for this OS."""
    if os.environ.get("GODOT"):
        return os.environ["GODOT"]
    system = platform.system()
    if system == "Windows":
        patterns = ["godot.windows.editor*.console.exe", "godot.windows.editor*.exe"]
    elif system == "Darwin":
        patterns = ["godot.macos.editor*"]
    else:
        patterns = ["godot.linuxbsd.editor*"]
    for pattern in patterns:
        found = [p for p in glob.glob(os.path.join(ROOT, "bin", pattern)) if os.path.isfile(p)]
        if found:
            return max(found, key=os.path.getmtime)
    raise ToolError("editor binary not found in bin/; build with target=editor or set GODOT", code=2)


def opt(args, name, default=""):
    key = "--" + name
    for i, a in enumerate(args):
        if a == key and i + 1 < len(args):
            return args[i + 1]
    return default


def run(cmd, timeout=300, env=None, cwd=None):
    """(exit code, combined output). A timeout counts as exit code 124, like timeout(1)."""
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, env=env, cwd=cwd)
        return p.returncode, p.stdout.decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired as e:
        return 124, (e.stdout or b"").decode("utf-8", errors="replace") + "\n(timed out)"


def render_command(cmd, size="1280x720"):
    """(command, env) for a run that draws frames. On Linux without a display this uses
    software Vulkan (lavapipe) on a virtual X display; elsewhere the real GPU."""
    env = dict(os.environ)
    software = os.environ.get("UZU_SOFTWARE_RENDER") == "1" or (platform.system() == "Linux" and not os.environ.get("DISPLAY"))
    if software:
        if not shutil.which("xvfb-run"):
            raise ToolError("rendering without a display needs xvfb-run (install xvfb)", code=2)
        if not os.path.exists(LAVAPIPE):
            raise ToolError(f"software rendering needs lavapipe ({LAVAPIPE}; install mesa-vulkan-drivers)", code=2)
        env["VK_ICD_FILENAMES"] = LAVAPIPE
        cmd = ["xvfb-run", "-a", "-s", f"-screen 0 {size}x24"] + cmd
    return cmd, env


def driver():
    return ["--rendering-driver", os.environ.get("UZU_RENDERING_DRIVER", "vulkan")]


def tail(text, n=20):
    return "\n".join(text.splitlines()[-n:])


def result_line(output, prefix):
    for line in output.splitlines():
        if line.startswith(prefix):
            return json.loads(line[len(prefix):])
    return None


def read_jsonl(path):
    items = []
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        items.append(json.loads(line))
                    except ValueError:
                        pass
    return items


def gd_tool(project, script, args, prefix, timeout=300):
    rc, out = run([find_godot(), "--headless", "--path", project, "--script", os.path.join(HERE, script), "--"] + args, timeout=timeout)
    result = result_line(out, prefix)
    if result is None:
        sys.stderr.write(out)
        raise ToolError("no result from the engine (see stderr)")
    return result, rc


# ---------------------------------------------------------------- engine


def engine(project, args):
    command = args[0] if args else ""
    godot = find_godot()
    if command == "run":
        frames = opt(args, "frames", "120")
        scene = opt(args, "scene")
        with tempfile.TemporaryDirectory() as tmp:
            jsonl = os.path.join(tmp, "errors.jsonl")
            rc, out = run([godot, "--headless", "--path", project, "--log-json", jsonl, "--quit-after", frames] + ([scene] if scene else []))
            errors = read_jsonl(jsonl)
        return {"ok": rc == 0, "command": "run", "exit_code": rc, "frames": int(frames), "errors": errors, "log_tail": tail(out)}
    if command == "check":
        path = opt(args, "path")
        if not path:
            raise ToolError("check needs --path res://script.gd")
        rc, out = run([godot, "--headless", "--path", project, "--check-only", "--json", "--script", path], timeout=120)
        result = next((json.loads(l) for l in out.splitlines() if l.startswith('{"ok":')), None)
        if result is None:  # The script could not be loaded at all.
            result = {"ok": False, "file": path, "errors": [{"file": path, "line": -1, "column": -1, "code": "LOAD", "message": tail(out, 10).strip()}], "warnings": []}
        result["ok"] = result["ok"] and rc == 0
        return {"command": "check", "path": path, **result}
    if command == "validate":
        result, _ = gd_tool(project, "engine_tool.gd", ["validate"], "ENGINE_RESULT ")
        for issue in result.get("issues", []):
            if issue["kind"] == "script_error":
                issue["errors"] = engine(project, ["check", "--path", issue["file"]])["errors"]
        return result
    if command == "shot":
        return look(project, args)
    result, _ = gd_tool(project, "engine_tool.gd", args, "ENGINE_RESULT ")
    return result


# ---------------------------------------------------------------- look


def screenshot(project, out, frames=8, size="1280x720"):
    """Renders the main scene and saves the last frame (Movie Maker)."""
    godot = find_godot()
    with tempfile.TemporaryDirectory() as tmp:
        args = driver() + ["--write-movie", os.path.join(tmp, "frame.png"), "--fixed-fps", "10", "--quit-after", str(frames)]
        cmd, env = render_command([godot, "--path", project] + args, size)
        rc, log = run(cmd, timeout=600, env=env, cwd=project)
        frames_written = sorted(glob.glob(os.path.join(tmp, "frame*.png")))
        if not frames_written:
            sys.stderr.write(log)
            raise ToolError("render failed: no frame was written (see stderr)", command="shot")
        os.makedirs(os.path.dirname(os.path.abspath(out)) or ".", exist_ok=True)
        shutil.copyfile(frames_written[-1], out)
    return {"ok": True, "command": "shot", "image": out}


def look(project, args):
    command = args[0] if args else "info"
    if command == "shot":
        out = opt(args, "out")
        if not out:
            raise ToolError("shot needs --out file.png")
        return screenshot(project, os.path.abspath(out), int(opt(args, "frames", "8")))
    if command == "check":
        image = opt(args, "image")
        if not image:
            raise ToolError("check needs --image file.png")
        rc, out = run([sys.executable, os.path.join(HERE, "palette_check.py"), image, "--json-only"], timeout=120)
        result = next((json.loads(l) for l in out.splitlines() if l.startswith("{")), None)
        if result is None:
            raise ToolError("image check failed: " + tail(out, 5))
        result.setdefault("ok", rc == 0)
        result.setdefault("command", "check")
        return result
    if command == "panorama":
        out = opt(args, "out")
        if not out:
            raise ToolError("panorama needs --out file.png")
        with tempfile.TemporaryDirectory() as faces:
            cmd, env = render_command([find_godot(), "--path", project] + driver() + ["--script", os.path.join(HERE, "panorama_capture.gd"), "--", faces])
            rc, log = run(cmd, timeout=900, env=env, cwd=project)
            if not os.path.exists(os.path.join(faces, "faces.json")):
                sys.stderr.write(log)
                raise ToolError("capture failed (see stderr)", command="panorama")
            rc, out_text = run([sys.executable, os.path.join(HERE, "panorama.py"), faces, os.path.abspath(out), "--width", opt(args, "width", "1024")], timeout=600)
        result = next((json.loads(l) for l in out_text.splitlines() if l.startswith("{")), None)
        return result or {"ok": rc == 0, "command": "panorama", "image": os.path.abspath(out), "log": tail(out_text, 5)}
    result, _ = gd_tool(project, "look_tool.gd", args, "UZU_RESULT ")
    return result


# ---------------------------------------------------------------- selfplay

AUTOLOADS_REPLAY = 'UZUReplay="*res://addons/uzu_selfplay/replay.gd"\nUZUTelemetry="*res://addons/uzu_selfplay/telemetry.gd"\n'


def install_addons(project_copy, files, autoloads):
    """Copies the given ai_loop files into res://addons/uzu_selfplay/ and prepends the
    autoloads, so they run before the game's own nodes. The original project is not touched."""
    addon = os.path.join(project_copy, "addons", "uzu_selfplay")
    os.makedirs(addon, exist_ok=True)
    for f in files:
        shutil.copy(os.path.join(HERE, f), addon)
    cfg = os.path.join(project_copy, "project.godot")
    with open(cfg, encoding="utf-8") as f:
        text = f.read()
    if "\n[autoload]\n" in text:
        text = text.replace("\n[autoload]\n", "\n[autoload]\n\n" + autoloads, 1)
    else:
        text += "\n[autoload]\n\n" + autoloads
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(text)


def selfplay(project, args):
    replay = opt(args, "input")
    if not replay or not os.path.isfile(replay):
        raise ToolError("selfplay needs --input replay.json")
    replay = os.path.abspath(replay)
    frames, out, movie, events_out, seed = opt(args, "frames", "1800"), opt(args, "out"), opt(args, "movie"), opt(args, "log"), opt(args, "seed")
    godot = find_godot()
    sys.path.insert(0, os.path.join(HERE, "telemetry"))
    import report as telemetry

    with tempfile.TemporaryDirectory() as work:
        game = os.path.join(work, "game")
        shutil.copytree(project, game)
        install_addons(game, ["telemetry/telemetry.gd", "selfplay/replay.gd"], AUTOLOADS_REPLAY)
        log, errors_path = os.path.join(work, "events.jsonl"), os.path.join(work, "errors.jsonl")
        base = ["--path", game, "--fixed-fps", "60", "--quit-after", frames, "--log-json", errors_path]
        user = ["--", "--uzu-telemetry", log, "--uzu-replay", replay] + (["--uzu-seed", seed] if seed else [])
        env = None
        if movie:
            movie = os.path.abspath(movie)
            cmd, env = render_command([godot] + driver() + ["--resolution", "640x360", "--write-movie", movie] + base + user, "640x360")
        else:
            cmd = [godot, "--headless"] + base + user
        rc, out_text = run(cmd, timeout=900, env=env)
        if events_out and os.path.exists(log):
            shutil.copyfile(log, events_out)
        if not os.path.exists(log):
            return {"ok": False, "command": "selfplay", "error": "no telemetry was written", "log_tail": tail(out_text)}
        events = telemetry.read_events([log])
        rep = telemetry.report(events)
        errors = read_jsonl(errors_path)
    quit_event = next((e for e in reversed(events) if e.get("type") == "quit"), {})
    result = {
        "ok": rc == 0,
        "command": "selfplay",
        "exit_code": rc,
        "cleared": rep["cleared"] == 1,
        "checkpoints": [c["id"] for c in rep["checkpoints"]],
        "deaths": rep["deaths"],
        "frames": max((e.get("frame", 0) for e in events), default=0),
        "fps": quit_event.get("fps"),
        "movie": movie or None,
        "errors": errors,
        "report": rep,
    }
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
    return result


# ---------------------------------------------------------------- self-tests


class Checks:
    def __init__(self):
        self.failed = 0

    def expect(self, name, condition, detail=""):
        if condition:
            print(f"ok   {name}")
        else:
            print(f"FAIL {name}: {detail}")
            self.failed += 1


def _call(fn, *a):
    try:
        return fn(*a)
    except ToolError as e:
        return {"ok": False, "error": e.message}


def selftest_engine():
    """The engine.sh self-test, in Python so it also runs on Windows."""
    c = Checks()
    with tempfile.TemporaryDirectory() as p:
        with open(os.path.join(p, "project.godot"), "w", encoding="utf-8") as f:
            f.write('config_version=5\n\n[application]\n\nconfig/name="engine selftest"\nrun/main_scene="res://main.tscn"\n')
            f.write('\n[input]\n\njump={\n"deadzone": 0.5,\n"events": [Object(InputEventKey,"resource_local_to_scene":false,"resource_name":"","device":-1,"window_id":0,"alt_pressed":false,"shift_pressed":false,"ctrl_pressed":false,"meta_pressed":false,"pressed":false,"keycode":0,"physical_keycode":32,"key_label":0,"unicode":32,"location":0,"echo":false,"script":null)\n]\n}\n')
        with open(os.path.join(p, "hello.gd"), "w", encoding="utf-8") as f:
            f.write('extends Node3D\nsignal poked\nfunc greet(who: String) -> String:\n\treturn "hello " + who\nfunc on_poked() -> void:\n\tpass\nfunc _ready() -> void:\n\tpush_error("selftest error from _ready")\n')
        with open(os.path.join(p, "broken.gd"), "w", encoding="utf-8") as f:
            f.write("extends Node\nfunc broken(:\n")
        e = lambda *a: _call(engine, p, list(a))  # noqa: E731
        S = ["--scene", "res://main.tscn"]
        c.expect("new", e("new", *S, "--type", "Node3D", "--name", "Main")["ok"])
        r = e("add", *S, "--parent", ".", "--type", "MeshInstance3D", "--name", "Box", "--set", '{"mesh": "BoxMesh", "position": "Vector3(0, 1, 0)"}')
        c.expect("add mesh", r["ok"] and r["node"]["type"] == "MeshInstance3D", r)
        r = e("get", *S, "--node", "Box", "--property", "position")
        c.expect("get", r.get("value") == "Vector3(0, 1, 0)", r)
        r = e("set", *S, "--node", "Box", "--property", "position", "--value", "Vector3(2, 0, -1)")
        c.expect("set", r["ok"] and r["value"] == "Vector3(2, 0, -1)", r)
        r = e("set", *S, "--node", "Box", "--property", "position:y", "--value", "3")
        c.expect("set indexed", r["ok"] and r["value"] == 3.0, r)
        c.expect("add group", e("add", *S, "--parent", ".", "--type", "Node3D", "--name", "Group")["ok"])
        r = e("reparent", *S, "--node", "Box", "--parent", "Group")
        c.expect("reparent", r["ok"] and r["node"]["path"] == "Group/Box", r)
        r = e("tree", *S)
        c.expect("tree", r["ok"] and [n["path"] for n in r["nodes"]] == [".", "Group", "Group/Box"], r)
        r = e("props", *S, "--node", "Group/Box")
        c.expect("props", r["ok"] and r["properties"]["mesh"] == {"class": "BoxMesh", "embedded": True} and r["properties"]["position"] == "Vector3(2, 3, -1)", r)
        r = e("script", *S, "--node", ".", "--path", "res://hello.gd")
        c.expect("script", r["ok"] and r["node"]["script"] == "res://hello.gd", r)
        r = e("call", *S, "--node", ".", "--method", "greet", "--args", '["agent"]')
        c.expect("call", r["ok"] and r["result"] == "hello agent", r)
        c.expect("connect", e("connect", *S, "--from", ".", "--signal", "poked", "--to", ".", "--method", "on_poked")["ok"])
        c.expect("connection saved", 'connection signal="poked"' in open(os.path.join(p, "main.tscn"), encoding="utf-8").read())
        r = e("classes", "--inherits", "Light3D")
        c.expect("classes", r["ok"] and "OmniLight3D" in r["classes"] and "Node2D" not in r["classes"], "")
        r = e("describe", "--class", "OmniLight3D")
        c.expect("describe", r["ok"] and any(x["name"] == "omni_range" for x in r["properties"]), "")
        c.expect("check good", e("check", "--path", "res://hello.gd")["ok"])
        r = e("check", "--path", "res://broken.gd")
        c.expect("check bad", not r["ok"] and r["errors"] and all(k in r["errors"][0] for k in ("file", "line", "column", "code", "message")) and r["errors"][0]["line"] > 0, r)
        r = e("run", "--frames", "5")
        c.expect("run", r["ok"] and any("selftest error from _ready" in x["message"] and x["code"].startswith("E") and x["file"] for x in r["errors"]), r)
        with open(os.path.join(p, "ref.tres"), "w", encoding="utf-8") as f:
            f.write('[gd_resource type="Resource" load_steps=2 format=3]\n\n[ext_resource type="Texture2D" path="res://missing.png" id="1"]\n\n[resource]\nmetadata/tex = ExtResource("1")\n')
        r = e("validate")
        c.expect("validate broken", not r["ok"] and {("script_error", "res://broken.gd"), ("missing_dependency", "res://ref.tres")} <= {(i["kind"], i["file"]) for i in r["issues"]} and any(x["line"] == 2 for i in r["issues"] for x in i.get("errors", [])), r)
        os.remove(os.path.join(p, "broken.gd"))
        os.remove(os.path.join(p, "ref.tres"))
        r = e("validate")
        c.expect("validate clean", r["ok"] and r["issues"] == [] and r["checked"] >= 2, r)
        r = e("actions")
        c.expect("actions", r["ok"] and [a["name"] for a in r["actions"]] == ["jump"] and r["actions"][0]["events"][0]["class"] == "InputEventKey" and "Space" in r["actions"][0]["events"][0]["text"], r)
        r = e("remove", *S, "--node", "Group/Box")
        c.expect("remove", r["ok"] and r["removed"] == "Group/Box", r)
        c.expect("bad node", not e("get", *S, "--node", "Nope", "--property", "position")["ok"])
        c.expect("bad scene", not e("tree", "--scene", "res://nope.tscn")["ok"])
        r = _call(look, p, ["info"])
        c.expect("look info", r["ok"] and "apply" in r["commands"], r)
    print("engine selftest: " + ("ok" if not c.failed else "FAILED"))
    return c.failed == 0


def selftest_selfplay():
    """The selfplay.sh self-test: same input, same play; seeds fix the random numbers."""
    game = os.path.join(HERE, "selfplay", "sample_game")
    replay = os.path.join(HERE, "selfplay", "sample_replay.json")
    with tempfile.TemporaryDirectory() as work:
        a = selfplay(game, ["--input", replay, "--frames", "600"])
        b = selfplay(game, ["--input", replay, "--frames", "600"])
        no_jump = os.path.join(work, "no_jump.json")
        with open(no_jump, "w") as f:
            f.write('{"events": [{"frame": 0, "action": "move_right", "pressed": true}]}\n')
        c = selfplay(game, ["--input", no_jump, "--frames", "600"])

        def main_items(r):
            rep = dict(r["report"])
            rep.pop("fps")  # Wall-clock speed differs between runs; nothing else may.
            return dict({k: r[k] for k in ("ok", "cleared", "checkpoints", "deaths", "frames", "errors")}, report=rep)

        assert a["ok"] and a["cleared"] and a["checkpoints"] == ["cp1", "cp2", "cp3"] and a["deaths"] == 1, a
        assert isinstance(a["fps"], (int, float)) and a["fps"] > 0, a
        assert main_items(a) == main_items(b), (main_items(a), main_items(b))
        assert not c["cleared"] and c["deaths"] > 1 and c["checkpoints"] == ["cp1"], c
        assert c["report"]["stuck_points"] and c["report"]["stuck_points"][0]["deaths"] == c["deaths"], c
        rand = os.path.join(work, "random_game")
        os.makedirs(rand)
        with open(os.path.join(rand, "project.godot"), "w") as f:
            f.write('config_version=5\n\n[application]\n\nrun/main_scene="res://main.tscn"\n')
        with open(os.path.join(rand, "main.tscn"), "w") as f:
            f.write('[gd_scene load_steps=2 format=3]\n\n[ext_resource type="Script" path="res://main.gd" id="1"]\n\n[node name="Main" type="Node"]\nscript = ExtResource("1")\n')
        with open(os.path.join(rand, "main.gd"), "w") as f:
            f.write('extends Node\n\nfunc _ready() -> void:\n\tget_node("/root/UZUTelemetry").checkpoint("r%d" % (randi() % 1000000))\n')
        s1, s1b, s2 = (selfplay(rand, ["--input", no_jump, "--frames", "5", "--seed", s])["checkpoints"] for s in ("1", "1", "2"))
        assert s1 == s1b and s1 != s2 and len(s1) == 1, (s1, s1b, s2)
    print("selfplay selftest: ok")
    return True


def selftest_tools():
    """The self-tests of the Python tools; each runs in its own interpreter."""
    tools = ["mcp_server.py", "bench.py", "prepublish.py", "aiplayer.py", "telemetry/report.py", "feedback/to_backlog.py", "bridge/mock_server.py", "bridge/client.py"]
    ok = True
    for tool in tools:
        rc, out = run([sys.executable, os.path.join(HERE, tool), "--self-test"], timeout=900)
        print(("ok   " if rc == 0 else "FAIL ") + tool + ("" if rc == 0 else ": " + tail(out, 8)))
        ok = ok and rc == 0
    print("tools selftest: " + ("ok" if ok else "FAILED"))
    return ok


def selftest(which):
    steps = {"engine": selftest_engine, "selfplay": selftest_selfplay, "tools": selftest_tools}
    names = list(steps) if which == "all" else [which]
    ok = True
    for name in names:
        try:
            ok = steps[name]() and ok
        except (AssertionError, ToolError) as e:
            print(f"{name} selftest: FAILED: {e}")
            ok = False
    return 0 if ok else 1


# ---------------------------------------------------------------- main


def main(argv):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")  # Windows consoles default to a local code page.
        except (AttributeError, ValueError):
            pass
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0 if argv else 2
    tool, rest = argv[0], argv[1:]
    if tool == "selftest":
        return selftest(rest[0] if rest else "all")
    if tool == "publish":
        sys.path.insert(0, os.path.join(HERE, "bridge"))
        import client

        return client.main(["publish"] + rest)
    if tool not in ("engine", "look", "selfplay") or not rest:
        print(__doc__, file=sys.stderr)
        return 2
    project = os.path.abspath(rest[0])
    if not os.path.exists(os.path.join(project, "project.godot")):
        print(json.dumps({"ok": False, "error": f"{rest[0]} is not a project (no project.godot)"}))
        return 2
    try:
        result = {"engine": engine, "look": look, "selfplay": selfplay}[tool](project, rest[1:])
    except ToolError as e:
        result = {"ok": False, "error": e.message, **e.extra}
        print(json.dumps(result, ensure_ascii=False))
        return e.code
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
