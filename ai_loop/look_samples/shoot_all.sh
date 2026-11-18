#!/usr/bin/env bash
# Usage: ai_loop/look_samples/shoot_all.sh <out-dir>
# Renders every look sample in its standard variations through ai_loop/look.sh,
# as an agent would, and writes <out-dir>/summary.json with the check of each
# shot. Use it to see the look, or to compare before and after an engine change
# (ai_loop/image_diff.py on matching file names).
set -euo pipefail

OUT="${1:?usage: shoot_all.sh <out-dir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LOOK="$HERE/../look.sh"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SUMMARY="$OUT/summary.json"
echo "[" >"$SUMMARY"
FIRST=1

# shot <name> <sample> [look.sh commands separated by ';' ...]
shot() {
  local name="$1" sample="$2" steps="${3:-}"
  local proj="$WORK/$name"
  cp -r "$HERE/$sample" "$proj"
  if [ -n "$steps" ]; then
    IFS=';' read -ra cmds <<<"$steps"
    for c in "${cmds[@]}"; do
      # shellcheck disable=SC2086
      "$LOOK" "$proj" $c >/dev/null
    done
  fi
  "$LOOK" "$proj" shot --out "$OUT/$name.png" --frames 8 >/dev/null
  [ $FIRST -eq 1 ] || echo "," >>"$SUMMARY"
  FIRST=0
  printf '{"shot": "%s", "steps": "%s", "check": %s}' "$name" "$steps" "$("$LOOK" "$proj" check --image "$OUT/$name.png")" >>"$SUMMARY"
  echo "shot: $name"
}

S="--scene res://main.tscn"
shot landscape_default landscape
for a in clear humid golden_hour; do
  shot "landscape_$a" landscape "apply $S --atmosphere $a"
done
shot street_default street
shot street_rig street "apply $S;rig $S"
shot street_night street "apply $S --atmosphere night;lamp $S --position -2.5,2.9,-1.5;lamp $S --position 3,2.9,-3.5;lamp $S --position -1,2.9,-6"
shot street_telephoto street "apply $S;camera $S --lens telephoto --eye standing"
shot materials_default materials
shot materials_presets materials "apply $S --atmosphere golden_hour;material $S --node Skin --kind skin;material $S --node Glass --kind glass;material $S --node Water --kind water;material $S --node Leaf0 --kind foliage;material $S --node Leaf1 --kind foliage;material $S --node Leaf2 --kind foliage;material $S --node Leaf3 --kind foliage;material $S --node Leaf4 --kind foliage"

shot water_default water "apply $S"
shot water_surface water "apply $S;water $S --position 0,0,-7.5 --size 16,21"
shot water_golden water "apply $S --atmosphere golden_hour;water $S --position 0,0,-7.5 --size 16,21"

echo "]" >>"$SUMMARY"
echo "summary: $SUMMARY"
