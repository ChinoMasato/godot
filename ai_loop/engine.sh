#!/usr/bin/env bash
# Usage: ai_loop/engine.sh <project-dir> <command> [options]   (see ai_loop/uzu.py engine, ai_loop/engine_tool.gd)
# Thin wrapper: the tool is ai_loop/uzu.py (cross-platform, B-801; on Windows run it with py).
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" engine "$@"
