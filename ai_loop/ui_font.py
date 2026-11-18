#!/usr/bin/env python3
"""Japanese font subsets for UI (BACKLOG B-1303).

A web build has no system fonts, so Japanese text needs a font shipped with the game
(the lesson of B-1003, where it came out garbled). Whole CJK fonts are large, so this
keeps only what the UI uses. It writes two WOFF2 files, each next to its license:
  <name>_latin.woff2  ASCII and Latin-1 from Open Sans (the main UI font)
  <name>_ja.woff2     Japanese from Droid Sans Japanese: every character of the UI specs,
                      plus the kana and Japanese punctuation the font has (so small text
                      changes still render); the UI uses it as the fallback of the first.

  ai_loop/ui_font.py --spec ui.json [--spec more.json] --out-dir fonts --name ui [--extra "text"]
                     [--font-ja file] [--font-latin file]

Prints one JSON line {"ok", "latin", "ja", "chars", "missing", "size_kb", "source_kb"};
"missing" lists characters the specs use that neither font has (an error).
Needs fontTools and brotli (pip install fonttools brotli); the rest is standard library.
"""

import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONTS = os.path.join(ROOT, "thirdparty", "fonts")
FONT_JA = os.path.join(FONTS, "DroidSansJapanese.woff2")
FONT_LATIN = os.path.join(FONTS, "OpenSans_SemiBold.woff2")
LICENSES = {FONT_JA: os.path.join(FONTS, "LICENSE.DroidSans.txt"), FONT_LATIN: os.path.join(FONTS, "LICENSE.OpenSans.txt")}
LATIN = "".join(chr(c) for c in range(0x20, 0x7F)) + "".join(chr(c) for c in range(0xA0, 0x100)) + "–—‘’“”•…€™"
# Kept when the Japanese font has them: CJK punctuation, hiragana, katakana, full-width forms.
BASE = "".join(chr(c) for c in range(0x3000, 0x3040))  # 、。「」『』〜 and other CJK punctuation
BASE += "".join(chr(c) for c in range(0x3040, 0x30A0))  # hiragana
BASE += "".join(chr(c) for c in range(0x30A0, 0x3100))  # katakana
BASE += "".join(chr(c) for c in range(0xFF01, 0xFF5F))  # full-width ASCII
BASE += "…‥・ー※★☆○●◎◇◆□■△▲▽▼→←↑↓"


def spec_text(node):
    """Every string a UI spec shows: texts, list items, tab titles (node names under Tabs)."""
    out = []
    if isinstance(node, dict):
        for key in ("text", "tooltip"):
            if isinstance(node.get(key), str):
                out.append(node[key])
        out += [str(i) for i in node.get("items", []) if isinstance(i, (str, int, float))]
        if node.get("type") == "Tabs":
            out += [c.get("name", "") for c in node.get("children", []) if isinstance(c, dict)]
        for child in node.get("children", []):
            out += spec_text(child)
        if "root" in node:
            out += spec_text(node["root"])
    return out


def _write(font, unicodes, out):
    from fontTools import subset as ft_subset
    from fontTools.ttLib import TTFont
    tt = TTFont(font)
    cmap = tt.getBestCmap()
    keep = [u for u in unicodes if u in cmap]
    options = ft_subset.Options()
    options.flavor = "woff2"
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    subsetter = ft_subset.Subsetter(options)
    subsetter.populate(unicodes=keep)
    subsetter.subset(tt)
    tt.flavor = "woff2"
    tt.save(out)
    lic = LICENSES.get(os.path.abspath(font))
    if lic and os.path.exists(lic):
        shutil.copyfile(lic, os.path.splitext(out)[0] + ".LICENSE.txt")
    return set(cmap), len(keep)


def subset(texts, out_dir, name="ui", font_ja=FONT_JA, font_latin=FONT_LATIN):
    try:
        import brotli  # noqa: F401  WOFF2 needs it.
        from fontTools import subset as _  # noqa: F401
    except ImportError:
        return {"ok": False, "error": "font subsets need fontTools and brotli: pip install fonttools brotli"}
    used = set("".join(texts)) - set("\n\r\t ")
    os.makedirs(out_dir, exist_ok=True)
    latin_out = os.path.join(out_dir, f"{name}_latin.woff2")
    ja_out = os.path.join(out_dir, f"{name}_ja.woff2")
    latin_has, latin_n = _write(font_latin, sorted({ord(c) for c in LATIN} | {ord(c) for c in used}), latin_out)
    ja_wanted = {ord(c) for c in BASE} | {ord(c) for c in used if ord(c) not in latin_has}
    ja_has, ja_n = _write(font_ja, sorted(ja_wanted), ja_out)
    missing = "".join(sorted(c for c in used if ord(c) not in latin_has and ord(c) not in ja_has))
    size = os.path.getsize(latin_out) + os.path.getsize(ja_out)
    source = os.path.getsize(font_latin) + os.path.getsize(font_ja)
    return {"ok": not missing, "latin": latin_out, "ja": ja_out, "chars": latin_n + ja_n, "used": len(used), "missing": missing,
            "size_kb": round(size / 1024, 1), "source_kb": round(source / 1024, 1),
            **({"error": f"no font has a glyph for {len(missing)} character(s) the UI uses; pass --font-ja with a fuller font"} if missing else {})}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--spec", action="append", default=[])
    parser.add_argument("--extra", default="")
    parser.add_argument("--font-ja", default=FONT_JA)
    parser.add_argument("--font-latin", default=FONT_LATIN)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--name", default="ui")
    a = parser.parse_args()
    texts = [a.extra]
    for path in a.spec:
        with open(path, encoding="utf-8") as f:
            texts += spec_text(json.load(f))
    result = subset(texts, a.out_dir, a.name, a.font_ja, a.font_latin)
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
