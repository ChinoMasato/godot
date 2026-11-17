#!/usr/bin/env bash
# Usage: ai_loop/publish.sh <build-dir | project-dir> [--preset Web] [--version 0.1] [--notes text] [--title T] [--game g_id] [--room r]
# One-command publish through the bridge API (BACKLOG B-505, ai_loop/bridge_api.md):
# export (for a project, with --preset) -> pre-publish check (B-502) -> register
# -> upload -> publish. Prints one JSON line with the play URL.
# Environment: UZU_BRIDGE_URL (default: the local mock, ai_loop/bridge/mock_server.py),
#              UZU_BRIDGE_TOKEN (the author's token; never written to the project).
# Feedback, telemetry and playtest sessions: ai_loop/bridge/client.py feedback|telemetry|sessions|join|events.
exec python3 "$(cd "$(dirname "$0")" && pwd)/bridge/client.py" publish "$@"
