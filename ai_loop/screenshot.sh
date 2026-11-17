#!/usr/bin/env bash
# Usage: ai_loop/screenshot.sh <project-dir> <out.png> [frames]
# Renders the project's main scene and saves the last frame (Movie Maker). Without a
# display (Linux) it uses software Vulkan on a virtual display; elsewhere the real GPU.
# Thin wrapper: the tool is ai_loop/uzu.py look shot (cross-platform, B-801).
P="${1:?usage: screenshot.sh <project-dir> <out.png> [frames]}"
O="${2:?usage: screenshot.sh <project-dir> <out.png> [frames]}"
exec python3 "$(cd "$(dirname "$0")" && pwd)/uzu.py" look "$P" shot --out "$O" --frames "${3:-5}"
