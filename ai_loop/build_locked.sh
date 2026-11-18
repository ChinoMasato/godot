#!/usr/bin/env bash
# Serialized build + full test run. Only one build runs at a time (flock),
# so parallel agents never run competing builds. Protocol: ai_loop/MULTI_AGENT.md
#
# Scons binary: set SCONS env var to override. Default: this VM's venv,
# falling back to `scons` on PATH (e.g. the owner's local PC).
# Target: the standard loop build is the editor (A-2, 2026-11-17: template
# builds never compile editor/ code and cannot use --path). Override with
# TARGET=template_release for runtime/export checks.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [ -z "${SCONS:-}" ]; then
  if [ -x "$HOME/workspace/.venv-build/bin/scons" ]; then
    SCONS="$HOME/workspace/.venv-build/bin/scons"
  else
    SCONS="scons"
  fi
fi

LOCK="/tmp/uzu-engine-build.lock"
exec 9>"$LOCK"
echo "waiting for build lock ($LOCK) ..."
flock 9
echo "build lock acquired."

TARGET="${TARGET:-editor}"
# Share the SCons cache with the Actions workflow (uzu_verify.yml): repeated
# and cross-branch builds reuse compiled objects instead of starting cold.
"$SCONS" platform=linuxbsd target="$TARGET" tests=yes accesskit=no arch=x86_64 -j3 \
  cache_path=.scons_cache cache_limit=7
"bin/godot.linuxbsd.${TARGET}.x86_64" --test
