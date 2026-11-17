#!/usr/bin/env python3
"""Aggregates play telemetry logs into a report (BACKLOG B-301).

Usage: ai_loop/telemetry/report.py <log.jsonl | directory> ... [--out report.json]
       ai_loop/telemetry/report.py --self-test

Formats: ai_loop/telemetry/FORMAT.md. The report is for the whole community
of plays given: no per-session rows, only counts. Standard library only.
"""

import json
import os
import statistics
import sys

VERSION = 1
HERE = os.path.dirname(os.path.abspath(__file__))


def read_events(paths):
    events = []
    for path in paths:
        files = [os.path.join(path, f) for f in sorted(os.listdir(path)) if f.endswith(".jsonl")] if os.path.isdir(path) else [path]
        for file in files:
            with open(file, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line:
                        try:
                            events.append(json.loads(line))
                        except ValueError:
                            pass  # A play cut off mid-write leaves a partial last line.
    return events


def report(events):
    sessions = {}
    for e in events:
        sessions.setdefault(e.get("session", ""), []).append(e)
    for evs in sessions.values():
        evs.sort(key=lambda e: e.get("frame", 0))

    points = {}  # (scene, cell) -> counts
    reached = {}  # checkpoint id -> first frames
    drop_off = {}
    cleared = deaths = 0
    fps = []
    for evs in sessions.values():
        seen_points = set()
        seen_checkpoints = set()
        last_checkpoint = None
        done = False
        for e in evs:
            kind = e.get("type")
            if kind in ("death", "stall") and "cell" in e:
                key = (e.get("scene", ""), tuple(e["cell"]))
                p = points.setdefault(key, {"deaths": 0, "stalls": 0, "sessions": 0})
                p["deaths" if kind == "death" else "stalls"] += 1
                if key not in seen_points:
                    seen_points.add(key)
                    p["sessions"] += 1
            if kind == "death":
                deaths += 1
            elif kind == "checkpoint":
                last_checkpoint = e.get("id")
                if last_checkpoint not in seen_checkpoints:
                    seen_checkpoints.add(last_checkpoint)
                    reached.setdefault(last_checkpoint, []).append(e.get("frame", 0))
            elif kind == "clear":
                done = True
            elif kind == "quit" and isinstance(e.get("fps"), (int, float)):
                fps.append(float(e["fps"]))
        if done:
            cleared += 1
        else:
            drop_off[last_checkpoint] = drop_off.get(last_checkpoint, 0) + 1

    stuck = [{"scene": s, "cell": list(c), **p} for (s, c), p in points.items() if p["deaths"] + p["stalls"] >= 2]
    stuck.sort(key=lambda p: (-(p["deaths"] + p["stalls"]), -p["sessions"], p["scene"], p["cell"]))
    checkpoints = [{"id": k, "sessions": len(v), "median_frame": statistics.median(v)} for k, v in reached.items()]
    checkpoints.sort(key=lambda c: (c["median_frame"], str(c["id"])))
    drops = [{"last_checkpoint": k, "sessions": n} for k, n in drop_off.items()]
    drops.sort(key=lambda d: (-d["sessions"], str(d["last_checkpoint"])))
    n = len(sessions)
    return {
        "v": VERSION,
        "sessions": n,
        "cleared": cleared,
        "clear_rate": round(cleared / n, 3) if n else 0.0,
        "deaths": deaths,
        "stuck_points": stuck,
        "checkpoints": checkpoints,
        "drop_off": drops,
        "fps": {"average": round(statistics.mean(fps), 1), "min": min(fps)} if fps else None,
    }


def self_test():
    r = report(read_events([os.path.join(HERE, "sample_logs")]))
    assert r["sessions"] == 3 and r["cleared"] == 1 and r["clear_rate"] == 0.333, r
    assert r["deaths"] == 4, r
    top = r["stuck_points"][0]
    assert top["cell"] == [100, 0, 0] and top["deaths"] == 3 and top["stalls"] == 1 and top["sessions"] == 2, r
    assert [c["id"] for c in r["checkpoints"]] == ["cp1", "cp2"] and r["checkpoints"][0]["sessions"] == 3, r
    assert {(d["last_checkpoint"], d["sessions"]) for d in r["drop_off"]} == {("cp1", 1), ("cp2", 1)}, r
    assert r["fps"] == {"average": 58.0, "min": 56.0}, r
    assert "session" not in json.dumps(r).replace('"sessions"', ""), "the report must not name sessions"
    print("telemetry report selftest: ok")
    return 0


def main():
    args = sys.argv[1:]
    if "--self-test" in args:
        return self_test()
    out = None
    if "--out" in args:
        i = args.index("--out")
        out = args[i + 1]
        del args[i : i + 2]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    text = json.dumps(report(read_events(args)), ensure_ascii=False)
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
