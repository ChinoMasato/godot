#!/usr/bin/env python3
"""B-1509 sample: the generation spec set of the generated village
(B-1508). Runs compose_spec_set.gd with the engine to dump the three
generations' record (the game's own specs from main.gd), then turns the
record into the four documents with ai_loop/spec_set.py:

    spec_set/scene_spec.json        intent + observed / inferred
    spec_set/region_graph.json      places and edges (B-1510 walks these)
    spec_set/asset_registry.json    part families, origin and counts
    spec_set/validation_report.json per-check Verified / Partial /
                                    Unverified with evidence
    spec_set/spec_record.json       the record the set was made from

Usage: python3 make_spec_set.py   (the editor binary: $GODOT or bin/)
Exit code 0 = the record was generated and the set written.
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from uzu import find_godot  # noqa: E402
import spec_set  # noqa: E402


def main():
    out_dir = os.path.join(HERE, "spec_set")
    os.makedirs(out_dir, exist_ok=True)
    record_path = os.path.join(out_dir, "spec_record.json")
    proc = subprocess.run(
        [find_godot(), "--headless", "--path", HERE, "--script", "res://compose_spec_set.gd",
         "--", "--record", record_path],
        capture_output=True, text=True, timeout=900)
    lines = [l for l in proc.stdout.splitlines() if l.startswith("UZU_RESULT ")]
    if not lines:
        sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
        print(json.dumps({"ok": False, "error": "compose_spec_set.gd printed no result"}))
        return 1
    generation = json.loads(lines[-1][len("UZU_RESULT "):])
    if not generation.get("ok"):
        print(json.dumps({"ok": False, "generation": generation}))
        return 1
    with open(record_path, encoding="utf-8") as f:
        record = json.load(f)
    written = spec_set.spec_set_from_record(record, out_dir)
    print(json.dumps({"ok": True, "generation": generation, "spec_set": written}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
