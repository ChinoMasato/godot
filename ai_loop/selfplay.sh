#!/usr/bin/env bash
# Usage: ai_loop/selfplay.sh <project-dir> --input replay.json [--frames 1800] [--out report.json] [--movie out.avi] [--log events.jsonl]
# Self-verification play, minimal version (BACKLOG B-303). Runs a copy of the
# project with recorded input and prints one JSON report: checkpoints reached,
# deaths, clear and average FPS, in the telemetry report format (B-301,
# ai_loop/telemetry/FORMAT.md), plus the engine errors of the run (B-201).
#
# The copy gets two autoloads: UZUTelemetry (ai_loop/telemetry/telemetry.gd) and
# the input replay (ai_loop/selfplay/replay.gd). The project itself is not changed.
# The game reports checkpoints, deaths and the clear through UZUTelemetry, and
# runs at a fixed 60 FPS (--fixed-fps), so the same input gives the same play.
# --movie also records the play with the Movie Maker (software Vulkan on a
# virtual display, as ai_loop/screenshot.sh); without it the run is headless.
# Environment: GODOT  editor binary (default: bin/godot.linuxbsd.editor.x86_64).
set -euo pipefail

PROJECT="$(cd "${1:?usage: selfplay.sh <project-dir> --input replay.json}" && pwd)"
shift
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
GODOT="${GODOT:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
opt() { local name="$1" default="$2"; shift 2; while [ $# -gt 0 ]; do [ "$1" = "--$name" ] && [ $# -gt 1 ] && { echo "$2"; return; }; shift; done; echo "$default"; }
INPUT="$(opt input "" "$@")"
FRAMES="$(opt frames 1800 "$@")"
OUT="$(opt out "" "$@")"
MOVIE="$(opt movie "" "$@")"
EVENTS="$(opt log "" "$@")"
[ -n "$INPUT" ] && [ -f "$INPUT" ] || { echo '{"ok": false, "error": "selfplay needs --input replay.json"}'; exit 1; }
INPUT="$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")"
[ -x "$GODOT" ] || { echo '{"ok": false, "error": "editor binary not found"}'; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -a "$PROJECT" "$WORK/game"
mkdir -p "$WORK/game/addons/uzu_selfplay"
cp "$ROOT/ai_loop/telemetry/telemetry.gd" "$ROOT/ai_loop/selfplay/replay.gd" "$WORK/game/addons/uzu_selfplay/"
# Prepend the autoloads so they run before the game's own nodes.
python3 -I - "$WORK/game/project.godot" <<'PY'
import sys
path = sys.argv[1]
text = open(path, encoding="utf-8").read()
lines = 'UZUReplay="*res://addons/uzu_selfplay/replay.gd"\nUZUTelemetry="*res://addons/uzu_selfplay/telemetry.gd"\n'
if "\n[autoload]\n" in text:
    text = text.replace("\n[autoload]\n", "\n[autoload]\n\n" + lines, 1)
else:
    text += "\n[autoload]\n\n" + lines
open(path, "w", encoding="utf-8").write(text)
PY

LOG="$WORK/events.jsonl"
ARGS=(--path "$WORK/game" --fixed-fps 60 --quit-after "$FRAMES" --log-json "$WORK/errors.jsonl")
if [ -n "$MOVIE" ]; then
  MOVIE="$(cd "$(dirname "$MOVIE")" && pwd)/$(basename "$MOVIE")"
  command -v xvfb-run >/dev/null || { echo '{"ok": false, "error": "--movie needs xvfb-run"}'; exit 2; }
  RUN=(env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 640x360x24" "$GODOT" --rendering-driver vulkan --resolution 640x360 --write-movie "$MOVIE" "${ARGS[@]}")
else
  RUN=("$GODOT" --headless "${ARGS[@]}")
fi
set +e
timeout 900 "${RUN[@]}" -- --uzu-telemetry "$LOG" --uzu-replay "$INPUT" >"$WORK/run.txt" 2>&1
RC=$?
set -e
[ -n "$EVENTS" ] && [ -f "$LOG" ] && cp "$LOG" "$EVENTS"

python3 -I - "$ROOT/ai_loop/telemetry" "$LOG" "$WORK/errors.jsonl" "$WORK/run.txt" "$RC" "$OUT" "$MOVIE" <<'PY'
import json, os, sys
telemetry_dir, log, errors_path, run_txt, rc, out, movie = sys.argv[1:8]
sys.path.insert(0, telemetry_dir)
import report as telemetry
if not os.path.exists(log):
    print(json.dumps({"ok": False, "command": "selfplay", "error": "no telemetry was written", "log_tail": "".join(open(run_txt, errors="replace").readlines()[-20:])}))
    sys.exit(1)
events = telemetry.read_events([log])
r = telemetry.report(events)
errors = []
if os.path.exists(errors_path):
    for line in open(errors_path, encoding="utf-8", errors="replace"):
        try:
            errors.append(json.loads(line))
        except ValueError:
            pass
quit_event = next((e for e in reversed(events) if e.get("type") == "quit"), {})
result = {
    "ok": int(rc) == 0,
    "command": "selfplay",
    "exit_code": int(rc),
    "cleared": r["cleared"] == 1,
    "checkpoints": [c["id"] for c in r["checkpoints"]],
    "deaths": r["deaths"],
    "frames": max((e.get("frame", 0) for e in events), default=0),
    "fps": quit_event.get("fps"),
    "movie": movie or None,
    "errors": errors,
    "report": r,
}
if out:
    with open(out, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=1)
print(json.dumps(result, ensure_ascii=False))
sys.exit(0 if result["ok"] else 1)
PY
