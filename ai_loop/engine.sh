#!/usr/bin/env bash
# Usage: ai_loop/engine.sh <project-dir> <command> [options]
# Operates a project and the engine from the command line, without the editor
# UI, so agents can use every engine feature (BACKLOG B-401). Prints one JSON
# line; exit 0 on success.
#
# Scene and class commands are in ai_loop/engine_tool.gd (tree, props, get, set,
# add, instance, remove, reparent, call, connect, script, new, classes, describe,
# actions).
# This wrapper adds:
#   run    [--frames 120] [--scene res://x.tscn]   runs the project headless; returns structured errors (B-201) and the log tail
#   check  --path res://a.gd                       checks a script; returns errors and warnings with line, column and code (B-202)
#   validate                                       loads every scene, resource and script; lists problems such as missing files (B-203)
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
    timeout 120 "$GODOT" --headless --path "$PROJECT" --check-only --json --script "$SCRIPT" >"$LOG" 2>&1
    RC=$?
    set -e
    # The engine prints one JSON line (B-202): errors and warnings with file, line, column, code, message.
    python3 -I - "$LOG" "$RC" "$SCRIPT" <<'PY'
import json, sys
log, rc, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
result = None
for line in open(log, encoding="utf-8", errors="replace"):
    if line.startswith('{"ok":'):
        try:
            result = json.loads(line)
        except ValueError:
            pass
if result is None:  # The script could not be loaded at all.
    tail = "".join(open(log, encoding="utf-8", errors="replace").readlines()[-10:])
    result = {"ok": False, "file": path, "errors": [{"file": path, "line": -1, "column": -1, "code": "LOAD", "message": tail.strip()}], "warnings": []}
result["ok"] = result["ok"] and rc == 0
print(json.dumps({"command": "check", "path": path, **result}))
PY
    exit $(( RC == 0 ? 0 : 1 ))
    ;;
  validate)
    # Loads every scene, resource and script (engine_tool.gd validate, B-203), then
    # adds each broken script's errors from the engine's JSON check (B-202).
    set +e
    "$GODOT" --headless --path "$PROJECT" --script "$ROOT/ai_loop/engine_tool.gd" -- validate >"$LOG" 2>&1
    RC=$?
    set -e
    python3 -I - "$LOG" "$GODOT" "$PROJECT" <<'PY'
import json, subprocess, sys
log, godot, project = sys.argv[1:4]
line = next((l for l in open(log, encoding="utf-8", errors="replace") if l.startswith("ENGINE_RESULT ")), None)
if line is None:
    print(json.dumps({"ok": False, "command": "validate", "error": "no result from the engine", "log_tail": "".join(open(log, encoding="utf-8", errors="replace").readlines()[-20:])}))
    sys.exit(1)
result = json.loads(line[len("ENGINE_RESULT "):])
for issue in result.get("issues", []):
    if issue["kind"] == "script_error":
        out = subprocess.run([godot, "--headless", "--path", project, "--check-only", "--json", "--script", issue["file"]], capture_output=True, text=True, timeout=120).stdout
        check = next((json.loads(l) for l in out.splitlines() if l.startswith('{"ok":')), None)
        issue["errors"] = check["errors"] if check else []
print(json.dumps(result))
sys.exit(0 if result.get("ok") else 1)
PY
    exit $?
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
