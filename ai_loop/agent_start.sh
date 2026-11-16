#!/usr/bin/env bash
# Usage: ai_loop/agent_start.sh <BACKLOG-ID> <agent-name>
# Creates a dedicated git worktree + branch for one backlog item,
# so multiple agents can develop in parallel without touching each other.
# Protocol: ai_loop/MULTI_AGENT.md
set -euo pipefail

ITEM="${1:?usage: agent_start.sh <BACKLOG-ID> <agent-name>}"
NAME="${2:?usage: agent_start.sh <BACKLOG-ID> <agent-name>}"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

LOWER="$(printf '%s' "$ITEM" | tr 'A-Z' 'a-z')"
BRANCH="agent/${LOWER}-${NAME}"
WT="${ROOT}/../wt-${LOWER}-${NAME}"

if git show-ref --verify --quiet "refs/heads/${BRANCH}"; then
  echo "branch already exists: ${BRANCH}" >&2
  exit 1
fi
if [ -e "$WT" ]; then
  echo "worktree path already exists: $WT" >&2
  exit 1
fi

git worktree add -b "$BRANCH" "$WT" ai-native
echo "worktree: $WT"
echo "branch:   $BRANCH"
echo "next: declare your claim in ai_loop/CLAIMS.md, then work inside the worktree."
