#!/usr/bin/env bash
# Self-test of the engine tools (engine.sh / uzu.py engine).
# Thin wrapper: the tool is ai_loop/uzu.py (cross-platform, B-801; on Windows run it with py).
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" selftest engine
