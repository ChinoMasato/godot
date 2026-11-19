# -*- coding: utf-8 -*-
"""Run inside Blender: build the B-1219 building street from a derived spec.

Usage (via ai_loop/building_recipe.py, not by hand):
  blender --background --factory-startup --disable-autoexec \
      --python ai_loop/blender/building_main.py -- <spec.json> <outdir>

The spec is the fully derived one written by building_recipe.py (all
seed variation already resolved there). Writes per-building GLBs and
texture PNGs, the three comparison renders and stats.json into outdir,
and prints a UZU_BUILDING_STATS line pointing at stats.json.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import building_lib  # noqa: E402


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    spec_path, outdir = args[0], args[1]
    with open(spec_path, encoding="utf-8") as fh:
        spec = json.load(fh)
    building_lib.run(spec, outdir)


if __name__ == "__main__":
    main()
