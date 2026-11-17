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
# Random seed (B-305): a game that draws random numbers gives the same draw for the same seed.
R="$WORK/random_game"; mkdir -p "$R"
printf 'config_version=5\n\n[application]\n\nrun/main_scene="res://main.tscn"\n' >"$R/project.godot"
printf '[gd_scene load_steps=2 format=3]\n\n[ext_resource type="Script" path="res://main.gd" id="1"]\n\n[node name="Main" type="Node"]\nscript = ExtResource("1")\n' >"$R/main.tscn"
printf 'extends Node\n\nfunc _ready() -> void:\n\tget_node("/root/UZUTelemetry").checkpoint("r%%d" %% (randi() %% 1000000))\n' >"$R/main.gd"
for name in s1 s1b s2; do
  seed_value=${name%b}; seed_value=${seed_value#s}
  "$HERE/selfplay.sh" "$R" --input "$WORK/no_jump.json" --frames 5 --seed "$seed_value" --out "$WORK/$name.json" >/dev/null
done
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
s1, s1b, s2 = (json.load(open(f"{work}/{n}.json"))["checkpoints"] for n in ("s1", "s1b", "s2"))
assert s1 == s1b and s1 != s2 and len(s1) == 1, (s1, s1b, s2)
print("selfplay selftest: ok")
PY
