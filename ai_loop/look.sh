#!/usr/bin/env bash
# Usage: ai_loop/look.sh <project-dir> <command> [options]
# Applies the U.ZU look to a project's scenes from the command line, without the
# editor UI, so agents can use it. Prints one JSON line; exit 0 on success.
#
#   ai_loop/look.sh my_game apply --scene res://main.tscn --atmosphere humid
#   ai_loop/look.sh my_game rig --scene res://main.tscn
#   ai_loop/look.sh my_game lamp --scene res://main.tscn --position 2,3,-4
#   ai_loop/look.sh my_game camera --scene res://main.tscn --lens telephoto --eye standing --shift 0.2
#   ai_loop/look.sh my_game material --kind water --out res://materials/water.tres
#   ai_loop/look.sh my_game info
#
# Then check it with ai_loop/screenshot.sh and ai_loop/palette_check.py.
# Environment: GODOT  editor binary (default: bin/godot.linuxbsd.editor.x86_64).
set -euo pipefail

PROJECT="${1:?usage: look.sh <project-dir> <command> [options]}"
shift
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
GODOT="${GODOT:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
[ -x "$GODOT" ] || { echo '{"ok": false, "error": "editor binary not found; build with target=editor"}'; exit 2; }

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT
set +e
"$GODOT" --headless --path "$PROJECT" --script "$ROOT/ai_loop/look_tool.gd" -- "$@" >"$LOG" 2>&1
RC=$?
set -e
LINE="$(grep -m1 '^UZU_RESULT ' "$LOG" | sed 's/^UZU_RESULT //')"
if [ -z "$LINE" ]; then
  cat "$LOG" >&2
  echo '{"ok": false, "error": "no result from the engine (see stderr)"}'
  exit 1
fi
echo "$LINE"
exit "$RC"
