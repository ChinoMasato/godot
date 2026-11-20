#!/usr/bin/env python3
"""Font coverage check for 『灯りの欠片』: every character the game can show
must have a glyph in art/game_font.ttf (a Noto Sans CJK subset).

The subset is small by design, so a new UI string can silently introduce a
character the font does not have — on the player's screen it renders as □
(tofu). That is how the touch UI labels and the loading label shipped broken
(owner playtest, 2026-11-21): the subset predates them. This check collects
every string literal in the game's scripts and UI specs and fails, listing
the missing characters, when the font does not cover them.

    python3 check_font_coverage.py [--font art/game_font.ttf]

Exit code 0 = covered. Prints one UZU_RESULT JSON line.
Regenerate the font with make_game_font.py when this fails.
Needs fontTools (pip install fonttools).
"""

import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Strings that are not display text: resource paths, file names, flags.
SKIP_RE = re.compile(r"^(res://|user://|--)|://|\.(gd|png|tscn|json|ttf|tres|import)$")


def gd_strings(path):
    """String literals of a GDScript file (comments included: over-covering
    is harmless, under-covering is the bug this check exists to catch)."""
    with open(path, encoding="utf-8") as f:
        src = f.read()
    out = []
    for m in STR_RE.finditer(src):
        s = m.group(1)
        if SKIP_RE.search(s):
            continue
        # GDScript escapes relevant here: \n and \" only in practice.
        out.append(s.replace("\\n", "\n").replace('\\"', '"'))
    return out


def json_strings(node, out):
    if isinstance(node, str):
        out.append(node)
    elif isinstance(node, dict):
        for v in node.values():
            json_strings(v, out)
    elif isinstance(node, list):
        for v in node:
            json_strings(v, out)


def collect_game_texts(project=HERE):
    texts = []
    for path in sorted(glob.glob(os.path.join(project, "*.gd"))):
        texts += gd_strings(path)
    for path in sorted(glob.glob(os.path.join(project, "ui", "*.json"))):
        with open(path, encoding="utf-8") as f:
            json_strings(json.load(f), texts)
    # The generated title scene carries the same texts as its spec, but scan
    # it too so a hand edit of the .tscn cannot slip a character past.
    for path in sorted(glob.glob(os.path.join(project, "ui", "*.tscn"))):
        texts += gd_strings(path)
    return texts


def collect_game_chars(project=HERE):
    used = set()
    for t in collect_game_texts(project):
        used.update(t)
    return used - set("\n\r\t")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", default=os.path.join(HERE, "art", "game_font.ttf"))
    parser.add_argument("--project", default=HERE)
    a = parser.parse_args()
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        print("UZU_RESULT " + json.dumps({"ok": False, "error": "fontTools is required: pip install fonttools"}))
        return 2
    font = TTFont(a.font)
    cmap = font.getBestCmap()
    used = collect_game_chars(a.project)
    missing = sorted(c for c in used if ord(c) not in cmap)
    result = {
        "ok": not missing,
        "font": a.font,
        "font_glyphs": len(cmap),
        "used_chars": len(used),
        "missing": ["U+%04X %s" % (ord(c), c) for c in missing],
    }
    if missing:
        result["error"] = "the font has no glyph for %d character(s) the game uses; regenerate it with make_game_font.py" % len(missing)
    print("UZU_RESULT " + json.dumps(result, ensure_ascii=False))
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main())
