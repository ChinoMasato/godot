#!/usr/bin/env bash
# Usage: ai_loop/selfplay.sh <project-dir> --input replay.json [--frames 1800] [--seed N] [--out report.json] [--movie out.avi] [--log events.jsonl]   (B-303)
# Thin wrapper: the tool is ai_loop/uzu.py (cross-platform, B-801; on Windows run it with py).
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" selfplay "$@"
