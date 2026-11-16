#!/usr/bin/env bash
# Usage: run inside your agent worktree before handing the branch to the lead.
# Verifies: incremental build (locked) + full unit test suite.
# Protocol: ai_loop/MULTI_AGENT.md
set -euo pipefail

"$(dirname "$0")/build_locked.sh"
echo "OK: build + full test suite passed in this worktree."
echo "Next: write your record under ai_loop/iterations/, commit, and hand the branch to the lead for merge."
