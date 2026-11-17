#!/usr/bin/env bash
# Self-test for ai_loop/selfplay.sh (BACKLOG B-303): the same input twice gives
# the same report (reproducibility); different input gives a different play.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$HERE/selfplay/sample_game"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
"$HERE/selfplay.sh" "$GAME" --input "$HERE/selfplay/sample_replay.json" --frames 600 --out "$WORK/a.json" >/dev/null
"$HERE/selfplay.sh" "$GAME" --input "$HERE/selfplay/sample_replay.json" --frames 600 --out "$WORK/b.json" >/dev/null
printf '{"events": [{"frame": 0, "action": "move_right", "pressed": true}]}\n' >"$WORK/no_jump.json"
"$HERE/selfplay.sh" "$GAME" --input "$WORK/no_jump.json" --frames 600 --out "$WORK/c.json" >/dev/null
python3 -I - "$WORK" <<'PY'
import json, sys
work = sys.argv[1]
a, b, c = (json.load(open(f"{work}/{n}.json")) for n in "abc")
def main_items(r):
    report = dict(r["report"])
    report.pop("fps")  # Wall-clock speed differs between runs; everything else must not.
    return {k: r[k] for k in ("ok", "cleared", "checkpoints", "deaths", "frames", "errors")} | {"report": report}
assert a["ok"] and a["cleared"] and a["checkpoints"] == ["cp1", "cp2", "cp3"] and a["deaths"] == 1, a
assert isinstance(a["fps"], (int, float)) and a["fps"] > 0, a
assert main_items(a) == main_items(b), (main_items(a), main_items(b))
assert not c["cleared"] and c["deaths"] > 1 and c["checkpoints"] == ["cp1"], c
assert c["report"]["stuck_points"] and c["report"]["stuck_points"][0]["deaths"] == c["deaths"], c
print("selfplay selftest: ok")
PY
