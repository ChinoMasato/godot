#!/usr/bin/env bash
# Self-test of the self-verification play (selfplay.sh / uzu.py selfplay, B-303, B-305).
# Thin wrapper: the tool is ai_loop/uzu.py (cross-platform, B-801; on Windows run it with py).
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" selftest selfplay
