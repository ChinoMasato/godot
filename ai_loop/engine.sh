#!/usr/bin/env bash
# Usage: ai_loop/engine.sh <project-dir> <command> [options]
# Operates a project and the engine from the command line, without the editor
# UI, so agents can use every engine feature (BACKLOG B-401). Prints one JSON
# line; exit 0 on success.
#
# Scene and class commands are in ai_loop/engine_tool.gd (tree, props, get, set,
# add, instance, remove, reparent, call, connect, script, new, classes, describe).
# This wrapper adds:
#   run    [--frames 120] [--scene res://x.tscn]   runs the project headless; returns structured errors (B-201) and the log tail
#   check  --path res://a.gd                       parses and compiles a script; returns errors with lines
#   shot   --out x.png [--frames 8]                renders without a GPU (ai_loop/screenshot.sh)
#
#   ai_loop/engine.sh my_game new --scene res://level.tscn --type Node3D
#   ai_loop/engine.sh my_game add --scene res://level.tscn --parent . --type MeshInstance3D --name Box --set '{"mesh": "BoxMesh"}'
#   ai_loop/engine.sh my_game set --scene res://level.tscn --node Box --property position --value 'Vector3(0, 1, 0)'
#   ai_loop/engine.sh my_game describe --class OmniLight3D
#
# The U.ZU look has its own command: ai_loop/look.sh.
# Environment: GODOT  editor binary (default: bin/godot.linuxbsd.editor.x86_64).
set -euo pipefail

PROJECT="${1:?usage: engine.sh <project-dir> <command> [options]}"
shift
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
GODOT="${GODOT:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
[ -x "$GODOT" ] || { echo '{"ok": false, "error": "editor binary not found; build with target=editor"}'; exit 2; }

opt() { # opt <name> <default> <args...>
  local name="$1" default="$2"; shift 2
  while [ $# -gt 0 ]; do
    if [ "$1" = "--$name" ] && [ $# -gt 1 ]; then echo "$2"; return; fi
    shift
  done
  echo "$default"
}

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

# Lines that report problems, as a JSON array (at most 50).
errors_json() {
  grep -E '^(ERROR|SCRIPT ERROR|USER ERROR|WARNING|USER WARNING|Parse Error|   at: )' "$1" | head -n 50 |
    python3 -I -c 'import json,sys; print(json.dumps([l.rstrip("\n") for l in sys.stdin]))'
}

case "${1:-}" in
  run)
    FRAMES="$(opt frames 120 "$@")"
    SCENE="$(opt scene "" "$@")"
    JSONL="$(mktemp)"
    set +e
    # shellcheck disable=SC2086
    timeout 300 "$GODOT" --headless --path "$PROJECT" --log-json "$JSONL" --quit-after "$FRAMES" $SCENE >"$LOG" 2>&1
    RC=$?
    set -e
    # Errors come from the engine's structured log (--log-json, B-201): level, code, file, line, function, message.
    python3 -I - "$JSONL" "$LOG" "$RC" "$FRAMES" <<'PY'
import json, sys
jsonl, log, rc, frames = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
errors = []
for line in open(jsonl, encoding="utf-8", errors="replace"):
    line = line.strip()
    if line:
        try:
            errors.append(json.loads(line))
        except ValueError:
            pass
tail = "".join(open(log, encoding="utf-8", errors="replace").readlines()[-20:])
print(json.dumps({"ok": rc == 0, "command": "run", "exit_code": rc, "frames": frames, "errors": errors, "log_tail": tail}))
PY
    rm -f "$JSONL"
    exit $(( RC == 0 ? 0 : 1 ))
    ;;
  check)
    SCRIPT="$(opt path "" "$@")"
    [ -n "$SCRIPT" ] || { echo '{"ok": false, "error": "check needs --path res://script.gd"}'; exit 1; }
    set +e
    timeout 120 "$GODOT" --headless --path "$PROJECT" --check-only --script "$SCRIPT" >"$LOG" 2>&1
    RC=$?
    set -e
    printf '{"ok": %s, "command": "check", "path": "%s", "errors": %s}\n' "$([ $RC -eq 0 ] && echo true || echo false)" "$SCRIPT" "$(errors_json "$LOG")"
    exit $(( RC == 0 ? 0 : 1 ))
    ;;
  shot)
    exec "$ROOT/ai_loop/look.sh" "$PROJECT" "$@"
    ;;
esac

set +e
"$GODOT" --headless --path "$PROJECT" --script "$ROOT/ai_loop/engine_tool.gd" -- "$@" >"$LOG" 2>&1
RC=$?
set -e
LINE="$(grep -m1 '^ENGINE_RESULT ' "$LOG" | sed 's/^ENGINE_RESULT //')"
if [ -z "$LINE" ]; then
  cat "$LOG" >&2
  echo '{"ok": false, "error": "no result from the engine (see stderr)"}'
  exit 1
fi
echo "$LINE"
exit "$RC"
