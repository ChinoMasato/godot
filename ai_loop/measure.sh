#!/usr/bin/env bash
# Usage: ai_loop/measure.sh [label]
# Measures one (incremental) build and one full unit-test run, then appends
# a single LEDGER-format line with the timings and the doctest summary.
# Builds are serialized with the same flock as build_locked.sh.
# Protocol: ai_loop/MULTI_AGENT.md (B-002)
#
# Environment:
#   SCONS  scons binary (default: this VM's venv, else `scons` on PATH)
#   JOBS   parallel build jobs (default: 3)
#   LEDGER file to append to (default: ai_loop/LEDGER.md). Non-lead agents
#          should point this at their own ai_loop/iterations/ record instead.
#
# Exit status: non-zero if the build or the test run failed. The line is
# appended either way, so failures are recorded too.
set -uo pipefail

LABEL="${1:-measure}"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [ -z "${SCONS:-}" ]; then
  if [ -x "$HOME/workspace/.venv-build/bin/scons" ]; then
    SCONS="$HOME/workspace/.venv-build/bin/scons"
  else
    SCONS="scons"
  fi
fi
JOBS="${JOBS:-3}"
LEDGER="${LEDGER:-ai_loop/LEDGER.md}"
BIN="bin/godot.linuxbsd.template_release.x86_64"
TEST_LOG="$(mktemp)"
trap 'rm -f "$TEST_LOG"' EXIT

LOCK="/tmp/uzu-engine-build.lock"
exec 9>"$LOCK"
echo "waiting for build lock ($LOCK) ..."
flock 9
echo "build lock acquired."

t0=$(date +%s)
"$SCONS" platform=linuxbsd target=template_release tests=yes accesskit=no arch=x86_64 -j"$JOBS"
build_rc=$?
t1=$(date +%s)

test_rc=-1
summary="not run"
if [ "$build_rc" -eq 0 ]; then
  "$BIN" --test 2>&1 | tee "$TEST_LOG"
  test_rc=${PIPESTATUS[0]}
  # doctest: "[doctest] test cases:   1431 |   1430 passed |    1 failed | 3 skipped"
  summary="$(grep -m1 '\[doctest\] test cases:' "$TEST_LOG" |
    sed -E 's/.*test cases: *([0-9]+) *\| *([0-9]+) passed *\| *([0-9]+) failed *\| *([0-9]+) skipped.*/cases \1, pass \2, fail \3, skip \4/')"
  [ -n "$summary" ] || summary="summary not found"
fi
t2=$(date +%s)

line="- $(date +%F) measure (${LABEL}) @ $(git rev-parse --short HEAD): build $((t1 - t0))s (exit ${build_rc}, -j${JOBS}) / test $((t2 - t1))s (exit ${test_rc}; ${summary}) on $(hostname), $(nproc) cores"
printf '%s\n' "$line" >>"$LEDGER"
echo "appended to ${LEDGER}:"
echo "$line"

[ "$build_rc" -eq 0 ] && [ "$test_rc" -eq 0 ]
