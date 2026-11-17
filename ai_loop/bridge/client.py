#!/usr/bin/env python3
"""Engine-side client of the bridge API (BACKLOG B-505..B-507), ai_loop/bridge_api.md v1.

Usage (through ai_loop/publish.sh, or directly):
  client.py publish <build-dir | project-dir> [--preset Web] [--version 0.1] [--notes text] [--title T] [--game g_id] [--room r]
  client.py feedback  [--game g_id | --project dir] [--since c_0] [--backlog out.md] [--gifts gifts.md]
  client.py telemetry [--game g_id | --project dir] [--build b_id]
  client.py sessions  [--game g_id | --project dir]
  client.py join   --session s_id
  client.py events --session s_id [--since c_0]
  client.py --self-test

Environment:
  UZU_BRIDGE_URL    API base (default http://127.0.0.1:8765, the mock: ai_loop/bridge/mock_server.py)
  UZU_BRIDGE_TOKEN  the author's token. Never written to the project.

publish with a project directory exports it first (--preset, needs export templates),
remembers the game in <project>/.uzu_publish.json ({"game_id"}; no token), runs the
pre-publish check (B-502) and only uploads when it passes. Prints one JSON line;
exit 1 on failure. Standard library only.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
AI_LOOP = os.path.dirname(HERE)
ROOT = os.path.dirname(AI_LOOP)
sys.path.insert(0, AI_LOOP)
sys.path.insert(0, os.path.join(AI_LOOP, "feedback"))
import prepublish  # noqa: E402
import to_backlog  # noqa: E402

STATE_FILE = ".uzu_publish.json"


class BridgeError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code, self.message = code, message


class Bridge:
    def __init__(self, url=None, token=None):
        self.url = (url or os.environ.get("UZU_BRIDGE_URL") or "http://127.0.0.1:8765").rstrip("/")
        self.token = token if token is not None else os.environ.get("UZU_BRIDGE_TOKEN", "")

    def call(self, method, path, body=None, raw=None, absolute=False):
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        req = urllib.request.Request(path if absolute else self.url + path, data=data, method=method)
        if not absolute and self.token:
            req.add_header("Authorization", f"Bearer {self.token}")
        if body is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=600) as resp:
                return json.loads(resp.read() or b"{}")
        except urllib.error.HTTPError as e:
            try:
                err = json.loads(e.read())["error"]
            except (ValueError, KeyError):
                err = {"code": f"http_{e.code}", "message": str(e)}
            raise BridgeError(err["code"], err["message"])
        except urllib.error.URLError as e:
            raise BridgeError("unreachable", f"cannot reach {self.url}: {e.reason}")


def _state(project):
    path = os.path.join(project, STATE_FILE)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _game_id(args):
    if args.get("game"):
        return args["game"]
    if args.get("project"):
        gid = _state(args["project"]).get("game_id")
        if gid:
            return gid
    raise BridgeError("bad_request", "give --game, or --project of a published project")


def _export(project, preset, out_dir):
    import uzu  # Finds the editor binary for this OS (B-801).

    godot = uzu.find_godot()
    os.makedirs(out_dir, exist_ok=True)
    proc = subprocess.run([godot, "--headless", "--path", project, "--export-release", preset, os.path.join(out_dir, "index.html")], capture_output=True, text=True, timeout=1800)
    if proc.returncode != 0 or not os.listdir(out_dir):
        text = re.sub(r"\x1b\[[0-9;]*m", "", proc.stdout + proc.stderr)  # Drop terminal colors.
        tail = [l.strip() for l in text.strip().splitlines()[-5:] if l.strip()]
        raise BridgeError("export_failed", f"export with preset '{preset}' failed (are the export templates installed?): " + " | ".join(tail))


def publish(bridge, target, args):
    is_project = os.path.exists(os.path.join(target, "project.godot"))
    work = tempfile.mkdtemp(prefix="uzu_publish_")
    try:
        build_dir = target
        if is_project:
            if not args.get("preset"):
                raise BridgeError("bad_request", "publishing a project needs --preset (or give the exported build directory)")
            build_dir = os.path.join(work, "build")
            _export(target, args["preset"], build_dir)
        check = prepublish.check(build_dir)
        if not check["ok"]:
            return {"ok": False, "command": "publish", "stage": "prepublish", "prepublish": check, "error": "the pre-publish check failed; nothing was uploaded"}
        archive = shutil.make_archive(os.path.join(work, "build"), "zip", build_dir)
        with open(archive, "rb") as f:
            payload = f.read()
        state = _state(target) if is_project else {}
        game_id = args.get("game") or state.get("game_id")
        if not game_id:
            title = args.get("title") or os.path.basename(os.path.abspath(target))
            game_id = bridge.call("POST", "/v1/games", {"title": title, "description": args.get("notes", ""), "room": args.get("room")})["game_id"]
            if is_project:
                with open(os.path.join(target, STATE_FILE), "w", encoding="utf-8") as f:
                    json.dump({"game_id": game_id}, f)  # The game only: the token never goes into the project.
        build = bridge.call("POST", f"/v1/games/{game_id}/builds", {"version": args.get("version", "0.1"), "platform": "web", "prototype": True, "notes": args.get("notes", ""), "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest(), "prepublish": check})
        bridge.call("PUT", build["upload_url"], raw=payload, absolute=True)
        done = bridge.call("POST", f"/v1/builds/{build['build_id']}/finalize")
        return {"ok": True, "command": "publish", "game_id": game_id, "build_id": build["build_id"], "status": done["status"], "play_url": done["play_url"], "size_mb": check["size_mb"]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def feedback(bridge, args):
    gid = _game_id(args)
    page = bridge.call("GET", f"/v1/games/{gid}/feedback" + (f"?since={args['since']}" if args.get("since") else ""))
    items = [{k: v for k, v in i.items() if k in to_backlog.FIELDS} for i in page["items"]]
    for n, item in enumerate(items, 1):
        to_backlog.validate(item, n)
    lines = to_backlog.to_backlog(items) if items else []
    if args.get("backlog"):
        with open(args["backlog"], "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + ("\n" if lines else ""))
    if args.get("gifts"):
        with open(args["gifts"], "w", encoding="utf-8") as f:
            f.write(to_backlog.gifts(items))
    return {"ok": True, "command": "feedback", "game_id": gid, "count": len(items), "ids": [i["id"] for i in page["items"]], "next": page["next"], "backlog": lines}


def telemetry(bridge, args):
    gid = _game_id(args)
    report = bridge.call("GET", f"/v1/games/{gid}/telemetry/report" + (f"?build={args['build']}" if args.get("build") else ""))
    summary = bridge.call("GET", f"/v1/games/{gid}/summary")
    return {"ok": True, "command": "telemetry", "game_id": gid, "summary": summary, "report": report}


def run(command, args, bridge=None):
    bridge = bridge or Bridge()
    try:
        if command == "publish":
            return publish(bridge, args["target"], args)
        if command == "feedback":
            return feedback(bridge, args)
        if command == "telemetry":
            return telemetry(bridge, args)
        if command == "sessions":
            gid = _game_id(args)
            return {"ok": True, "command": "sessions", "game_id": gid, **bridge.call("GET", f"/v1/games/{gid}/sessions")}
        if command == "join":
            return {"ok": True, "command": "join", **bridge.call("POST", f"/v1/sessions/{args['session']}/join")}
        if command == "events":
            return {"ok": True, "command": "events", **bridge.call("GET", f"/v1/sessions/{args['session']}/events" + (f"?since={args['since']}" if args.get("since") else ""))}
        raise BridgeError("bad_request", f"unknown command {command}")
    except BridgeError as e:
        return {"ok": False, "command": command, "error": {"code": e.code, "message": e.message}}
    except (KeyError, to_backlog.FeedbackError) as e:
        return {"ok": False, "command": command, "error": {"code": "bad_request", "message": str(e)}}


def self_test():
    import threading

    sys.path.insert(0, HERE)
    import mock_server

    server = mock_server.serve(port=0)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    bridge = Bridge(server.RequestHandlerClass.base_url, "dev-token")
    tmp = tempfile.mkdtemp()
    try:
        # B-505: a build without the notices is stopped before anything is sent.
        build = os.path.join(tmp, "build")
        os.makedirs(build)
        with open(os.path.join(build, "index.html"), "w") as f:
            f.write("<html>game</html>")
        r = run("publish", {"target": build, "title": "穴とびランナー"}, bridge)
        assert not r["ok"] and r["stage"] == "prepublish", r
        assert not server.RequestHandlerClass.store.games and not server.RequestHandlerClass.store.builds, "nothing must be sent"
        prepublish.add_notices(build)
        r = run("publish", {"target": build, "title": "穴とびランナー", "version": "0.1"}, bridge)
        assert r["ok"] and r["status"] == "live" and r["play_url"].endswith(r["build_id"]), r
        gid = r["game_id"]
        r2 = run("publish", {"target": build, "game": gid, "version": "0.2"}, bridge)
        assert r2["ok"] and r2["game_id"] == gid and r2["build_id"] != r["build_id"], r2
        # A project remembers its game (and never the token); without a preset it asks for one.
        project = os.path.join(tmp, "project")
        os.makedirs(project)
        open(os.path.join(project, "project.godot"), "w").write("config_version=5\n")
        assert run("publish", {"target": project}, bridge)["error"]["code"] == "bad_request"
        assert run("feedback", {"project": project}, bridge)["error"]["code"] == "bad_request"
        bad = run("publish", {"target": build, "game": "g_nope"}, bridge)
        assert not bad["ok"] and bad["error"]["code"] == "not_found", bad
        assert run("publish", {"target": build, "title": "x"}, Bridge(bridge.url, "wrong"))["error"]["code"] == "unauthorized"
        # B-506: feedback becomes backlog items and the gift file; telemetry gives report and summary.
        post = lambda path, body: bridge.call("POST", path, body)  # noqa: E731
        words = "2つ目の穴、何回やっても落ちる…でも越えたときは気持ちよかった！"
        post("/mock/feedback", {"game_id": gid, "v": 1, "kind": "stuck", "message": words, "where": {"scene": "res://main.tscn", "checkpoint": "cp2"}})
        post("/mock/feedback", {"game_id": gid, "v": 1, "kind": "bug", "message": "止まった", "steps": ["ゴールまで行く"]})
        backlog, gifts = os.path.join(tmp, "fb.md"), os.path.join(tmp, "gifts.md")
        fb = run("feedback", {"game": gid, "backlog": backlog, "gifts": gifts}, bridge)
        assert fb["ok"] and fb["count"] == 2 and len(fb["backlog"]) == 2 and fb["backlog"][0].startswith("- [ ] F-001 [不具合]"), fb
        assert words in open(gifts, encoding="utf-8").read() and words not in open(backlog, encoding="utf-8").read()
        assert run("feedback", {"game": gid, "since": fb["next"]}, bridge)["count"] == 0
        for n in range(5):
            sid = f"s-{n:016x}"
            post("/mock/telemetry", {"game_id": gid, "events": [{"session": sid, "type": "start", "frame": 0}, {"session": sid, "type": "death", "frame": 9, "scene": "res://main.tscn", "cell": [75, 0, 0]}]})
        t = run("telemetry", {"game": gid}, bridge)
        assert t["ok"] and t["summary"]["players"] == 5 and t["summary"]["feedback"] == 2 and t["report"]["stuck_points"][0]["deaths"] == 5, t
        # B-507: sessions, join and events with since.
        sid = post("/mock/sessions", {"game_id": gid, "room": "class-a", "starts": "2026-11-20T15:00+09:00"})["session_id"]
        assert run("sessions", {"game": gid}, bridge)["sessions"][0]["session_id"] == sid
        assert run("join", {"session": sid}, bridge)["join_url"].endswith("as=author")
        post(f"/mock/sessions/{sid}/player", {})
        ev = run("events", {"session": sid}, bridge)
        assert [e["type"] for e in ev["events"]] == ["player_joined"], ev
        post(f"/mock/sessions/{sid}/player", {})
        assert run("events", {"session": sid, "since": ev["next"]}, bridge)["events"] == [{"type": "player_joined", "count": 2}]
        assert run("join", {"session": "s_nope"}, bridge)["error"]["code"] == "not_found"
        assert not run("events", {"session": sid}, Bridge("http://127.0.0.1:9", "x"))["ok"]  # Unreachable is an error, not a crash.
    finally:
        server.shutdown()
        shutil.rmtree(tmp, ignore_errors=True)
    print("bridge client selftest: ok")
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    if "--self-test" in argv:
        return self_test()
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    command, rest, args = argv[0], argv[1:], {}
    if command == "publish":
        if not rest or rest[0].startswith("--"):
            print("publish needs a build or project directory", file=sys.stderr)
            return 2
        args["target"] = os.path.abspath(rest.pop(0))
    while rest:
        key = rest.pop(0)
        if not key.startswith("--") or not rest:
            print(f"bad argument {key}", file=sys.stderr)
            return 2
        args[key[2:]] = rest.pop(0)
    result = run(command, args)
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
