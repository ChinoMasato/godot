#!/usr/bin/env bash
# Usage: ai_loop/screenshot.sh <project-dir> <out.png> [frames]
# Renders a project's main scene without a GPU or a real display and saves the
# last frame as a PNG, so agents can check visual changes themselves.
# Uses software Vulkan (Mesa lavapipe) + a virtual X display (Xvfb) + Movie Maker.
# Compare two shots with ai_loop/image_diff.py. Protocol: ai_loop/MULTI_AGENT.md
#
# Environment:
#   GODOT  engine binary (default: bin/godot.linuxbsd.editor.x86_64, else the
#          template_release binary; template builds cannot take --path, so the
#          binary is copied next to the project for the run)
#   SIZE   virtual screen size (default: 1280x720x24)
#
# Needs: xvfb-run (xvfb) and the lavapipe ICD (mesa-vulkan-drivers).
set -euo pipefail

PROJECT="${1:?usage: screenshot.sh <project-dir> <out.png> [frames]}"
OUT="${2:?usage: screenshot.sh <project-dir> <out.png> [frames]}"
FRAMES="${3:-5}"

ROOT="$(git rev-parse --show-toplevel)"
PROJECT="$(cd "$PROJECT" && pwd)"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"

if [ -z "${GODOT:-}" ]; then
  if [ -x "$ROOT/bin/godot.linuxbsd.editor.x86_64" ]; then
    GODOT="$ROOT/bin/godot.linuxbsd.editor.x86_64"
  else
    GODOT="$ROOT/bin/godot.linuxbsd.template_release.x86_64"
  fi
fi
ICD=/usr/share/vulkan/icd.d/lvp_icd.json
[ -f "$ICD" ] || { echo "lavapipe not found ($ICD): install mesa-vulkan-drivers" >&2; exit 2; }
command -v xvfb-run >/dev/null || { echo "xvfb-run not found: install xvfb" >&2; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ARGS=(--rendering-driver vulkan --write-movie "$WORK/frame.png" --fixed-fps 10 --quit-after "$FRAMES")
case "$GODOT" in
  *template*)
    # Template builds ignore --path: run a copy placed in the project directory.
    BIN="$PROJECT/.screenshot_runner"
    cp "$GODOT" "$BIN"
    trap 'rm -rf "$WORK"; rm -f "$BIN"' EXIT
    RUN=("$BIN" "${ARGS[@]}")
    ;;
  *)
    RUN=("$GODOT" --path "$PROJECT" "${ARGS[@]}")
    ;;
esac

(cd "$PROJECT" && VK_ICD_FILENAMES="$ICD" xvfb-run -a -s "-screen 0 ${SIZE:-1280x720x24}" "${RUN[@]}") >"$WORK/log.txt" 2>&1 || {
  cat "$WORK/log.txt" >&2
  echo "render failed" >&2
  exit 1
}

LAST="$(ls "$WORK"/frame*.png 2>/dev/null | sort | tail -n 1)"
[ -n "$LAST" ] || { cat "$WORK/log.txt" >&2; echo "no frame was written" >&2; exit 1; }
cp "$LAST" "$OUT"
grep -m1 '^Vulkan' "$WORK/log.txt" || true
echo "saved: $OUT"
