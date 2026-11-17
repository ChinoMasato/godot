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
#   ai_loop/look.sh my_game shot --out /tmp/shot.png [--frames 8]
#   ai_loop/look.sh my_game check --image /tmp/shot.png
#   ai_loop/look.sh my_game panorama --out /tmp/pano.png [--width 1024]
#   ai_loop/look.sh my_game info
#
# shot renders the main scene without a GPU (ai_loop/screenshot.sh); check reports
# color balance, muddiness and composition (ai_loop/palette_check.py).
# Environment: GODOT  editor binary (default: bin/godot.linuxbsd.editor.x86_64).
set -euo pipefail

PROJECT="${1:?usage: look.sh <project-dir> <command> [options]}"
shift
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

opt() { # opt <name> <default> <args...>
  local name="$1" default="$2"; shift 2
  while [ $# -gt 0 ]; do
    if [ "$1" = "--$name" ] && [ $# -gt 1 ]; then echo "$2"; return; fi
    shift
  done
  echo "$default"
}

case "${1:-}" in
  shot)
    OUT="$(opt out "" "$@")"
    [ -n "$OUT" ] || { echo '{"ok": false, "error": "shot needs --out file.png"}'; exit 1; }
    if MSG="$("$ROOT/ai_loop/screenshot.sh" "$PROJECT" "$OUT" "$(opt frames 8 "$@")" 2>&1)"; then
      printf '{"ok": true, "command": "shot", "image": "%s"}\n' "$OUT"
      exit 0
    fi
    printf '%s\n' "$MSG" >&2
    echo '{"ok": false, "command": "shot", "error": "render failed (see stderr)"}'
    exit 1
    ;;
  panorama)
    OUT="$(opt out "" "$@")"
    [ -n "$OUT" ] || { echo '{"ok": false, "error": "panorama needs --out file.png"}'; exit 1; }
    GODOT="${GODOT:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
    FACES="$(mktemp -d)"
    trap 'rm -rf "$FACES"' EXIT
    if ! (cd "$PROJECT" && VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 1280x720x24" \
        "$GODOT" --path "$PROJECT" --rendering-driver vulkan --script "$ROOT/ai_loop/panorama_capture.gd" -- "$FACES") >"$FACES/log.txt" 2>&1 \
        || [ ! -f "$FACES/faces.json" ]; then
      cat "$FACES/log.txt" >&2
      echo '{"ok": false, "command": "panorama", "error": "capture failed (see stderr)"}'
      exit 1
    fi
    python3 "$ROOT/ai_loop/panorama.py" "$FACES" "$OUT" --width "$(opt width 1024 "$@")"
    exit $?
    ;;
  check)
    IMAGE="$(opt image "" "$@")"
    [ -n "$IMAGE" ] || { echo '{"ok": false, "error": "check needs --image file.png"}'; exit 1; }
    python3 "$ROOT/ai_loop/palette_check.py" "$IMAGE" --json-only
    exit $?
    ;;
esac
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
