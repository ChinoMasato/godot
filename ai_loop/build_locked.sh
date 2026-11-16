#!/usr/bin/env bash
# Serialized build + full test run. Only one build runs at a time (flock),
# so parallel agents never run competing builds. Protocol: ai_loop/MULTI_AGENT.md
#
# Scons binary: set SCONS env var to override. Default: this VM's venv,
# falling back to `scons` on PATH (e.g. the owner's local PC).
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

"$SCONS" platform=linuxbsd target=template_release tests=yes accesskit=no arch=x86_64 -j3
bin/godot.linuxbsd.template_release.x86_64 --test
