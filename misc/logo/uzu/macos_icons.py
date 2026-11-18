"""Generate the macOS bundle icons (.icns) from the committed official U.ZU icon.

python misc/logo/uzu/macos_icons.py            # write the .icns files
python misc/logo/uzu/macos_icons.py --check    # only check that they match the artwork

Requires Pillow and Playwright (Chromium) to render misc/logo/icon.svg at 1024 px,
like generate_assets.py --raster. No artwork is downloaded or redrawn. The icon
follows Apple's icon grid: the rounded square is 824 px inside a 1024 px canvas.
"""

import argparse
import io
import json
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[3]
SOURCE = "misc/logo/icon.svg"
CANVAS = 1024
ARTWORK = 824  # Apple's macOS icon grid: 100 px margins around the rounded square.
ICONS = (
    "misc/dist/macos_template.app/Contents/Resources/icon.icns",  # Exported games without their own icon.
    "misc/dist/macos_tools.app/Contents/Resources/GodotLG.icns",  # The editor bundle (historical file name).
)


NODE_RENDER = """
const { chromium } = require("playwright");
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: %d, height: %d }, deviceScaleFactor: 1 });
  await page.setContent(require("fs").readFileSync(0, "utf8"));
  process.stdout.write(await page.screenshot({ omitBackground: true }));
  await browser.close();
})();
"""


def render():
    html = (
        "<style>html,body{margin:0;background:transparent;width:100%;height:100%}"
        "svg{width:100%;height:100%;display:block}</style>" + (ROOT / SOURCE).read_text(encoding="utf-8")
    )
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:  # No Python Playwright: use Node's (require("playwright")).
        import subprocess

        png = subprocess.run(["node", "-e", NODE_RENDER % (ARTWORK, ARTWORK)], input=html.encode(), capture_output=True, check=True).stdout
    else:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            page = browser.new_page(viewport={"width": ARTWORK, "height": ARTWORK}, device_scale_factor=1)
            page.set_content(html)
            png = page.screenshot(omit_background=True)
            browser.close()
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    margin = (CANVAS - ARTWORK) // 2
    canvas.paste(Image.open(io.BytesIO(png)).convert("RGBA"), (margin, margin))
    return canvas


def largest(path):
    with Image.open(path) as icns:
        w, h, scale = max(icns.info["sizes"], key=lambda s: s[0] * s[2])  # e.g. (512, 512, 2) = 1024 px.
        icns.size = (w, h)
        icns.load(scale=scale)
        return icns.convert("RGBA"), sorted(icns.info["sizes"])


def matches(path, expected):
    """True when the file's largest image is the artwork (within PNG/rounding noise)."""
    actual, _ = largest(path)
    if actual.size != expected.size:
        return False
    diff = ImageChops.difference(actual, expected).getextrema()
    return max(high for _, high in diff) <= 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="only check the files against the artwork")
    args = parser.parse_args()
    icon = render()
    report = {"ok": True, "source": SOURCE, "files": {}}
    for name in ICONS:
        path = ROOT / name
        if not args.check:
            icon.save(path, format="ICNS")
        ok = matches(path, icon)
        report["files"][name] = {"matches_artwork": ok, "sizes": [list(s) for s in largest(path)[1]]}
        report["ok"] = report["ok"] and ok
    print(json.dumps(report))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
