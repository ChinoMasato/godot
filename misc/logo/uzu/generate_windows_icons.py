#!/usr/bin/env python3
"""Convert the stored official SVG to both Windows executable icons.

Requires Pillow, Playwright and Chromium. --check compares every stored ICO
frame with a fresh rendering and verifies editor/template resource references.
This converts the original artwork; it does not redesign or fetch the logo.
"""

import argparse
import io
import shutil
import struct
from pathlib import Path

from PIL import Image, ImageChops
from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SIZES = (16, 24, 32, 48, 64, 128, 256)
TARGETS = (ROOT / "platform/windows/godot.ico", ROOT / "platform/windows/godot_console.ico")
REFERENCES = {
    "godot_res.rc": "godot.ico",
    "godot_res_template.rc": "godot.ico",
    "godot_res_wrap.rc": "godot_console.ico",
    "godot_res_wrap_template.rc": "godot_console.ico",
}


def render(chromium: str) -> dict[int, Image.Image]:
    source = (HERE / "official-icon.svg").read_text(encoding="utf-8")
    frames = {}
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=chromium, args=["--no-sandbox"])
        try:
            for size in SIZES:
                page = browser.new_page(viewport={"width": size, "height": size}, device_scale_factor=1)
                page.set_content(
                    "<style>html,body{margin:0;background:transparent;width:100%;height:100%}"
                    "svg{display:block;width:100%;height:100%}</style>" + source
                )
                frames[size] = Image.open(io.BytesIO(page.screenshot(omit_background=True))).convert("RGBA")
                page.close()
        finally:
            browser.close()
    return frames


def verify(frames: dict[int, Image.Image]) -> None:
    for target in TARGETS:
        data = target.read_bytes()
        if struct.unpack_from("<HHH", data) != (0, 1, len(SIZES)):
            raise ValueError(f"{target}: invalid ICO directory")
        with Image.open(target) as icon:
            if icon.ico.sizes() != {(size, size) for size in SIZES}:
                raise ValueError(f"{target}: expected icon sizes {SIZES}")
            for size, expected in frames.items():
                actual = icon.ico.getimage((size, size)).convert("RGBA")
                # Check all channels: RGBA getbbox alone can miss RGB-only differences.
                if any(channel.getbbox() for channel in ImageChops.difference(actual, expected).split()):
                    raise ValueError(f"{target}: {size}px differs from official-icon.svg")
        print(f"ok: {target.relative_to(ROOT)} ({len(SIZES)} RGBA frames)")
    if TARGETS[0].read_bytes() != TARGETS[1].read_bytes():
        raise ValueError("editor and console executable icons differ")
    for filename, icon in REFERENCES.items():
        source = (ROOT / "platform/windows" / filename).read_text(encoding="utf-8")
        if f"GODOT_ICON ICON platform/windows/{icon}" not in source:
            raise ValueError(f"{filename}: unexpected embedded icon reference")
    print("ok: editor, console wrapper and export-template resource references")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify checked-in assets without writing them")
    parser.add_argument("--chromium", default=shutil.which("chromium") or shutil.which("chromium-browser"))
    options = parser.parse_args()
    if not options.chromium:
        parser.error("Chromium not found; pass --chromium /path/to/chromium")
    frames = render(options.chromium)
    if not options.check:
        output = io.BytesIO()
        frames[256].save(
            output,
            format="ICO",
            sizes=[(size, size) for size in SIZES],
            append_images=[frames[size] for size in SIZES if size != 256],
        )
        for target in TARGETS:
            target.write_bytes(output.getvalue())
    verify(frames)


if __name__ == "__main__":
    main()
