#!/usr/bin/env bash
# Usage: ai_loop/publish.sh <build-dir | project-dir> [--preset Web] [--version 0.1] [--notes text] [--title T] [--game g_id]   (B-505; UZU_BRIDGE_URL, UZU_BRIDGE_TOKEN)
# Thin wrapper: the tool is ai_loop/uzu.py (cross-platform, B-801; on Windows run it with py).
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" publish "$@"
