#!/usr/bin/env python3
"""Local mock of the bridge API (BACKLOG B-504), for testing the engine side
until the play side (Gogashare) exists. Implements ai_loop/bridge_api.md v1.

Usage: ai_loop/bridge/mock_server.py [--port 8765] [--token dev-token]
       ai_loop/bridge/mock_server.py --self-test

State is in memory. Test data goes in through endpoints that are NOT part of
the contract (they only exist in the mock):
  POST /mock/feedback   one feedback item (B-601 format) for a game: {"game_id", "build_id"?, ...item}
  POST /mock/telemetry  event-log lines of one play (B-301 format): {"game_id", "build_id"?, "events": [...]}
  POST /mock/sessions   a playtest session: {"game_id", "room", "starts"}
  POST /mock/sessions/{id}/player   a player joins (adds a player_joined event)
Standard library only.
"""

import hashlib
import json
import os
import re
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "telemetry"))
sys.path.insert(0, os.path.join(HERE, "..", "feedback"))
import report as telemetry_report  # noqa: E402
import to_backlog as feedback_format  # noqa: E402

MIN_SESSIONS_FOR_PLACES = 5  # Below this, stuck points and drop-off are hidden (Q9).
MAX_UPLOAD = 1000 * 1024 * 1024


class ApiError(Exception):
    def __init__(self, status, code, message):
        super().__init__(message)
        self.status, self.code, self.message = status, code, message


class Store:
    def __init__(self):
        self.lock = threading.Lock()
        self.games, self.builds, self.uploads = {}, {}, {}
        self.feedback, self.plays, self.sessions = [], [], {}
        self.counter = 0

    def new_id(self, prefix):
        self.counter += 1
        return f"{prefix}_{self.counter:04x}"


def _page(items, since, key="items"):
    start = int(since[2:]) if since and since.startswith("c_") else 0
    return {key: items[start:], "next": f"c_{len(items)}"}


def _require(body, *names):
    missing = [n for n in names if body.get(n) in (None, "")]
    if missing:
        raise ApiError(400, "bad_request", f"missing {', '.join(missing)}")


class Handler(BaseHTTPRequestHandler):
    store = None
    token = "dev-token"
    base_url = ""

    def log_message(self, *args):
        pass

    def _send(self, status, body):
        data = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_UPLOAD:
            raise ApiError(413, "too_large", "the upload is too large")
        return self.rfile.read(length) if length else b""

    def _json(self):
        raw = self._body()
        try:
            return json.loads(raw or b"{}")
        except ValueError:
            raise ApiError(400, "bad_request", "the body is not JSON")

    def _handle(self, method):
        try:
            url = urllib.parse.urlsplit(self.path)
            query = dict(urllib.parse.parse_qsl(url.query))
            path = url.path
            if path.startswith("/upload/"):
                status, body = self._upload(path, query)
            else:
                if not path.startswith("/mock/") and self.headers.get("Authorization") != f"Bearer {self.token}":
                    raise ApiError(401, "unauthorized", "missing or wrong token")
                status, body = self._route(method, path, query)
        except ApiError as e:
            status, body = e.status, {"error": {"code": e.code, "message": e.message}}
        except feedback_format.FeedbackError as e:
            status, body = 400, {"error": {"code": "bad_request", "message": str(e)}}
        self._send(status, body)

    def do_GET(self):
        self._handle("GET")

    def do_POST(self):
        self._handle("POST")

    def do_PUT(self):
        self._handle("PUT")

    def _upload(self, path, query):
        build_id = path.split("/")[2]
        build = self.store.builds.get(build_id)
        if build is None or query.get("key") != build["upload_key"]:
            raise ApiError(404, "not_found", "no such upload")
        data = self._body()
        with self.store.lock:
            self.store.uploads[build_id] = data
        return 200, {"received": len(data)}

    def _game(self, game_id):
        game = self.store.games.get(game_id)
        if game is None:
            raise ApiError(404, "not_found", f"no game {game_id}")
        return game

    def _route(self, method, path, query):
        s = self.store
        m = lambda pattern: re.fullmatch(pattern, path)  # noqa: E731
        with s.lock:
            # 1. Register a game.
            if method == "POST" and path == "/v1/games":
                body = self._json()
                _require(body, "title")
                gid = s.new_id("g")
                s.games[gid] = {"game_id": gid, "title": body["title"], "description": body.get("description", ""), "room": body.get("room")}
                return 201, {"game_id": gid, "page_url": f"{self.base_url}/g/{gid}"}
            # 2. Start a build.
            if method == "POST" and (r := m(r"/v1/games/([\w-]+)/builds")):
                self._game(r[1])
                body = self._json()
                _require(body, "version", "platform", "size", "sha256", "prepublish")
                if body["platform"] != "web":
                    raise ApiError(400, "bad_request", "platform must be web")
                if not body["prepublish"].get("ok"):
                    problems = "; ".join(p.get("message", p.get("kind", "")) for p in body["prepublish"].get("problems", []))
                    raise ApiError(422, "prepublish_failed", problems or "the pre-publish check did not pass")
                if int(body["size"]) > MAX_UPLOAD:
                    raise ApiError(413, "too_large", "the build is too large")
                bid = s.new_id("b")
                key = hashlib.sha256(f"{bid}{s.counter}".encode()).hexdigest()[:16]
                s.builds[bid] = {"build_id": bid, "game_id": r[1], "version": body["version"], "prototype": body.get("prototype", True), "notes": body.get("notes", ""), "size": int(body["size"]), "sha256": body["sha256"], "status": "uploading", "upload_key": key, "published": None}
                return 201, {"build_id": bid, "upload_url": f"{self.base_url}/upload/{bid}?key={key}", "upload_expires_in": 900}
            # 4. Finalize a build.
            if method == "POST" and (r := m(r"/v1/builds/([\w-]+)/finalize")):
                build = s.builds.get(r[1])
                if build is None:
                    raise ApiError(404, "not_found", f"no build {r[1]}")
                data = s.uploads.get(r[1])
                if data is None:
                    raise ApiError(400, "bad_request", "nothing was uploaded")
                if len(data) != build["size"] or hashlib.sha256(data).hexdigest() != build["sha256"]:
                    raise ApiError(400, "bad_request", "the upload does not match the size and sha256 given")
                build["status"], build["published"] = "live", "2026-11-17"
                return 200, {"build_id": r[1], "status": "live", "play_url": f"{self.base_url}/play/{r[1]}"}
            # 5. Builds of a game.
            if method == "GET" and (r := m(r"/v1/games/([\w-]+)/builds")):
                self._game(r[1])
                builds = [{k: b[k] for k in ("build_id", "version", "status", "published")} | {"play_url": f"{self.base_url}/play/{b['build_id']}"} for b in s.builds.values() if b["game_id"] == r[1]]
                return 200, {"builds": builds}
            # 6. Feedback.
            if method == "GET" and (r := m(r"/v1/games/([\w-]+)/feedback")):
                self._game(r[1])
                items = [f["item"] for f in s.feedback if f["game_id"] == r[1]]
                return 200, _page(items, query.get("since"))
            # 7. Telemetry report.
            if method == "GET" and (r := m(r"/v1/games/([\w-]+)/telemetry/report")):
                self._game(r[1])
                build = query.get("build")
                events = [e for p in s.plays if p["game_id"] == r[1] and (not build or p["build_id"] == build) for e in p["events"]]
                rep = telemetry_report.report(events)
                if rep["sessions"] < MIN_SESSIONS_FOR_PLACES:
                    rep["stuck_points"], rep["drop_off"] = [], []
                return 200, rep
            # 8. Summary.
            if method == "GET" and (r := m(r"/v1/games/([\w-]+)/summary")):
                self._game(r[1])
                players = len({e.get("session") for p in s.plays if p["game_id"] == r[1] for e in p["events"]})
                feedback = sum(1 for f in s.feedback if f["game_id"] == r[1])
                return 200, {"players": players, "feedback": feedback, "first_player_waiting": players == 0}
            # 9. Sessions of a game.
            if method == "GET" and (r := m(r"/v1/games/([\w-]+)/sessions")):
                self._game(r[1])
                return 200, {"sessions": [{k: v for k, v in x.items() if k in ("session_id", "room", "starts", "status")} for x in s.sessions.values() if x["game_id"] == r[1]]}
            # 10. Join a session.
            if method == "POST" and (r := m(r"/v1/sessions/([\w-]+)/join")):
                if r[1] not in s.sessions:
                    raise ApiError(404, "not_found", f"no session {r[1]}")
                return 200, {"join_url": f"{self.base_url}/live/{r[1]}?as=author", "role": "author"}
            # 11. Session events.
            if method == "GET" and (r := m(r"/v1/sessions/([\w-]+)/events")):
                if r[1] not in s.sessions:
                    raise ApiError(404, "not_found", f"no session {r[1]}")
                return 200, _page(s.sessions[r[1]]["events"], query.get("since"), key="events")
            # Mock-only test data.
            if method == "POST" and path == "/mock/feedback":
                body = self._json()
                gid, bid = body.pop("game_id", None), body.pop("build_id", None)
                self._game(gid)
                feedback_format.validate(body, len(s.feedback) + 1)
                fid = s.new_id("f")
                s.feedback.append({"game_id": gid, "item": {"id": fid, "build_id": bid, **body}})
                for x in s.sessions.values():
                    if x["game_id"] == gid and x["status"] == "live":
                        x["events"].append({"type": "feedback", "id": fid})
                return 201, {"id": fid}
            if method == "POST" and path == "/mock/telemetry":
                body = self._json()
                self._game(body.get("game_id"))
                s.plays.append({"game_id": body["game_id"], "build_id": body.get("build_id"), "events": body.get("events", [])})
                return 201, {"plays": len(s.plays)}
            if method == "POST" and path == "/mock/sessions":
                body = self._json()
                self._game(body.get("game_id"))
                sid = s.new_id("s")
                s.sessions[sid] = {"session_id": sid, "game_id": body["game_id"], "room": body.get("room"), "starts": body.get("starts"), "status": body.get("status", "live"), "events": [], "players": 0}
                return 201, {"session_id": sid}
            if method == "POST" and (r := m(r"/mock/sessions/([\w-]+)/player")):
                x = s.sessions.get(r[1])
                if x is None:
                    raise ApiError(404, "not_found", f"no session {r[1]}")
                x["players"] += 1
                x["events"].append({"type": "player_joined", "count": x["players"]})
                return 201, {"count": x["players"]}
        raise ApiError(404, "not_found", f"no endpoint {method} {path}")


def serve(port=8765, token="dev-token", host="127.0.0.1"):
    handler = type("BoundHandler", (Handler,), {"store": Store(), "token": token})
    server = ThreadingHTTPServer((host, port), handler)
    handler.base_url = f"http://{host}:{server.server_address[1]}"
    return server


def self_test():
    import urllib.request

    server = serve(port=0)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = server.RequestHandlerClass.base_url

    def call(method, path, body=None, token="dev-token", raw=None, url=None):
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        req = urllib.request.Request(url or base + path, data=data, method=method)
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        try:
            with urllib.request.urlopen(req) as resp:
                return resp.status, json.loads(resp.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    try:
        assert call("POST", "/v1/games", {"title": "x"}, token="wrong")[0] == 401
        st, game = call("POST", "/v1/games", {"title": "穴とびランナー", "room": "class-a"})
        assert st == 201 and game["game_id"], game
        gid = game["game_id"]
        assert call("POST", "/v1/games", {})[1]["error"]["code"] == "bad_request"
        # Publishing.
        payload = b"PK fake build" * 100
        meta = {"version": "0.3", "platform": "web", "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
        st, err = call("POST", f"/v1/games/{gid}/builds", meta | {"prepublish": {"ok": False, "problems": [{"kind": "engine_license_missing", "message": "license missing"}]}})
        assert st == 422 and err["error"]["code"] == "prepublish_failed", err
        st, b = call("POST", f"/v1/games/{gid}/builds", meta | {"prepublish": {"ok": True, "problems": []}})
        assert st == 201 and b["upload_url"].startswith(base), b
        assert call("POST", f"/v1/builds/{b['build_id']}/finalize")[0] == 400  # Nothing uploaded yet.
        assert call("PUT", "", raw=payload, url=b["upload_url"], token=None)[0] == 200
        st, fin = call("POST", f"/v1/builds/{b['build_id']}/finalize")
        assert st == 200 and fin["status"] == "live" and fin["play_url"].endswith(b["build_id"]), fin
        assert call("GET", f"/v1/games/{gid}/builds")[1]["builds"][0]["status"] == "live"
        # Feedback: scores are refused, words come back untouched.
        assert call("POST", "/mock/feedback", {"game_id": gid, "v": 1, "kind": "idea", "message": "x", "stars": 5})[0] == 400
        words = "2つ目の穴、何回やっても落ちる…"
        call("POST", "/mock/feedback", {"game_id": gid, "build_id": b["build_id"], "v": 1, "kind": "stuck", "message": words, "where": {"checkpoint": "cp2"}})
        st, fb = call("GET", f"/v1/games/{gid}/feedback")
        assert st == 200 and fb["items"][0]["message"] == words and fb["items"][0]["build_id"] == b["build_id"], fb
        call("POST", "/mock/feedback", {"game_id": gid, "v": 1, "kind": "bug", "message": "音が二重"})
        later = call("GET", f"/v1/games/{gid}/feedback?since={fb['next']}")[1]
        assert [i["kind"] for i in later["items"]] == ["bug"], later
        # Telemetry: places hidden below five plays, shown from five.
        assert call("GET", f"/v1/games/{gid}/summary")[1] == {"players": 0, "feedback": 2, "first_player_waiting": True}
        def play(n, death):
            events = [{"v": 1, "session": f"s-{n:016x}", "type": "start", "frame": 0}, {"v": 1, "session": f"s-{n:016x}", "type": "checkpoint", "frame": 50, "id": "cp1"}]
            if death:
                events.append({"v": 1, "session": f"s-{n:016x}", "type": "death", "frame": 90, "scene": "res://main.tscn", "cell": [75, 0, 0]})
            call("POST", "/mock/telemetry", {"game_id": gid, "build_id": b["build_id"], "events": events})
        for n in range(4):
            play(n, True)
        rep = call("GET", f"/v1/games/{gid}/telemetry/report")[1]
        assert rep["sessions"] == 4 and rep["deaths"] == 4 and rep["stuck_points"] == [] and rep["drop_off"] == [], rep
        play(4, True)
        rep = call("GET", f"/v1/games/{gid}/telemetry/report?build={b['build_id']}")[1]
        assert rep["sessions"] == 5 and rep["stuck_points"][0]["cell"] == [75, 0, 0] and rep["drop_off"], rep
        summary = call("GET", f"/v1/games/{gid}/summary")[1]
        assert summary == {"players": 5, "feedback": 2, "first_player_waiting": False}, summary
        assert not {"rating", "stars", "score", "rank", "sales"} & set(json.dumps(summary)), summary
        # Playtest sessions.
        sid = call("POST", "/mock/sessions", {"game_id": gid, "room": "class-a", "starts": "2026-11-20T15:00+09:00"})[1]["session_id"]
        assert call("GET", f"/v1/games/{gid}/sessions")[1]["sessions"][0]["session_id"] == sid
        assert call("POST", f"/v1/sessions/{sid}/join")[1]["role"] == "author"
        call("POST", f"/mock/sessions/{sid}/player")
        call("POST", "/mock/feedback", {"game_id": gid, "v": 1, "kind": "idea", "message": "目印がほしい"})
        ev = call("GET", f"/v1/sessions/{sid}/events")[1]
        assert [e["type"] for e in ev["events"]] == ["player_joined", "feedback"], ev
        call("POST", f"/mock/sessions/{sid}/player")
        assert call("GET", f"/v1/sessions/{sid}/events?since={ev['next']}")[1]["events"] == [{"type": "player_joined", "count": 2}]
        assert call("GET", "/v1/sessions/nope/events")[1]["error"]["code"] == "not_found"
    finally:
        server.shutdown()
    print("bridge mock selftest: ok")
    return 0


def main():
    args = sys.argv[1:]
    if "--self-test" in args:
        return self_test()
    port = int(args[args.index("--port") + 1]) if "--port" in args else 8765
    token = args[args.index("--token") + 1] if "--token" in args else "dev-token"
    server = serve(port, token)
    print(f"bridge mock on {server.RequestHandlerClass.base_url} (token: {token})", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
