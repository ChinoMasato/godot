# -*- coding: utf-8 -*-
"""Run inside Blender: build the B-1219 stage-2 rocks from a derived spec.

Usage (via ai_loop/rock_recipe.py, not by hand):
  blender --background --factory-startup --disable-autoexec \
      --python ai_loop/blender/rock_main.py -- <spec.json> <outdir>

The spec is the fully derived one written by rock_recipe.py (all seed
variation already resolved there). Writes per-rock GLBs and texture
PNGs, the three comparison renders and stats.json into outdir, and
prints a UZU_ROCK_STATS line pointing at stats.json.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import rock_lib  # noqa: E402


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    spec_path, outdir = args[0], args[1]
    with open(spec_path, encoding="utf-8") as fh:
        spec = json.load(fh)
    rock_lib.run(spec, outdir)


if __name__ == "__main__":
    main()
