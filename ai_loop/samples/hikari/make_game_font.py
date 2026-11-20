#!/usr/bin/env python3
"""Regenerates art/game_font.ttf, the Noto Sans CJK JP subset this game ships.

The subset keeps:
  * every character the game's strings use (collected by
    check_font_coverage.py, so a new UI string is covered by regenerating),
  * the full hiragana and katakana blocks, CJK punctuation and full-width
    forms (so small text edits still render),
  * printable ASCII and a set of basic symbols.

    python3 make_game_font.py --source /path/to/NotoSansCJK-Regular.ttc [--face 0]

--source is a full Noto Sans CJK (TTC or OTF/TTF); face 0 of the official
TTC is JP Regular, the face the shipped font was cut from. The source font
is not in this repository (it is large); Noto Sans CJK is OFL 1.1, see
art/FONT_LICENSE.txt. Prints one UZU_RESULT JSON line.
Needs fontTools (pip install fonttools).
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from check_font_coverage import collect_game_chars  # noqa: E402

# Blocks kept whole, so wording tweaks do not need a font rebuild each time.
BLOCKS = [
    (0x0020, 0x007E),  # printable ASCII
    (0x00A0, 0x00FF),  # Latin-1 supplement
    (0x3000, 0x303F),  # CJK punctuation
    (0x3040, 0x309F),  # hiragana, all of it
    (0x30A0, 0x30FF),  # katakana, all of it (long-vowel mark and small kana included)
    (0xFF01, 0xFF5E),  # full-width ASCII forms
]
SYMBOLS = "…‥※★☆○●◎◇◆□■△▲▽▼→←↑↓＋−＝＜＞％＃＠＆＊／＼｜〜～「」『』【】〈〉（）"


def keep_set(project):
    keep = {ord(c) for c in collect_game_chars(project)}
    for lo, hi in BLOCKS:
        keep.update(range(lo, hi + 1))
    keep.update(ord(c) for c in SYMBOLS)
    return keep


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, help="full Noto Sans CJK font (TTC/OTF/TTF)")
    parser.add_argument("--face", type=int, default=0, help="face index inside a TTC (0 = JP)")
    parser.add_argument("--out", default=os.path.join(HERE, "art", "game_font.ttf"))
    parser.add_argument("--project", default=HERE)
    a = parser.parse_args()
    try:
        from fontTools import subset as ft_subset
        from fontTools.ttLib import TTFont
    except ImportError:
        print("UZU_RESULT " + json.dumps({"ok": False, "error": "fontTools is required: pip install fonttools"}))
        return 2
    tt = TTFont(a.source, fontNumber=a.face)
    family = tt["name"].getDebugName(1)
    cmap = tt.getBestCmap()
    wanted = keep_set(a.project)
    keep = sorted(u for u in wanted if u in cmap)
    # Only a character the game itself uses is fatal when the source font
    # lacks it; the whole-block ranges include unassigned codepoints
    # (U+3040, U+3097...) that no font has, and those are simply skipped.
    used = {ord(c) for c in collect_game_chars(a.project)}
    missing_from_source = sorted(u for u in used if u not in cmap)
    options = ft_subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    subsetter = ft_subset.Subsetter(options)
    subsetter.populate(unicodes=keep)
    subsetter.subset(tt)
    tt.save(a.out)
    result = {
        "ok": not missing_from_source,
        "out": a.out,
        "source_family": family,
        "glyphs_kept": len(keep),
        "size_kb": round(os.path.getsize(a.out) / 1024, 1),
        "missing_from_source": ["U+%04X" % u for u in missing_from_source],
    }
    print("UZU_RESULT " + json.dumps(result, ensure_ascii=False))
    return 0 if not missing_from_source else 1


if __name__ == "__main__":
    sys.exit(main())
