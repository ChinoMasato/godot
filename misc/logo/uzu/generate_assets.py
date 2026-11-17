#!/usr/bin/env python3
"""Export the owner's official U.ZU identity for engine branding.

Run from any directory. Add --raster to export PNGs using Playwright/Chromium.
The original website SVGs and the traced wordmark are stored alongside this
script; regeneration never downloads or changes the website.
"""

import argparse
import copy
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)


def tag(name):
    return f"{{{NS}}}{name}"


def svg(width, height, view_box):
    return ET.Element(tag("svg"), width=str(width), height=str(height), viewBox=view_box, fill="none")


def write(path, root):
    destination = ROOT / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    ET.indent(root)
    destination.write_text(ET.tostring(root, encoding="unicode") + "\n", encoding="utf-8")


def identity_parts():
    official = ET.parse(HERE / "official-identity.svg").getroot()
    gradient = copy.deepcopy(official.find(f"{tag('defs')}/{tag('linearGradient')}"))
    orbit = copy.deepcopy(official.find(tag("g")))
    center = copy.deepcopy(official.find(tag("circle")))
    paths = list(ET.parse(HERE / "wordmark-paths.svg").getroot())
    return gradient, orbit, center, paths


def add_identity(root, *, mark=True, wordmark=True):
    gradient, orbit, center, paths = identity_parts()
    defs = ET.SubElement(root, tag("defs"))
    defs.append(gradient)
    if mark:
        root.extend((orbit, center))
    if wordmark:
        # The website masks the lettering with an embedded PNG. Replace only
        # that mask with traced paths so the engine SVG loader needs no raster.
        sx, sy = 411 / 748, 104.4 / 200
        lettering_gradient = copy.deepcopy(gradient)
        lettering_gradient.set("id", "wordmark-metal")
        lettering_gradient.set("gradientTransform", f"matrix({1 / sx} 0 0 {1 / sy} {-183 / sx} {-37.8 / sy})")
        defs.append(lettering_gradient)
        group = ET.SubElement(root, tag("g"), transform=f"translate(183 37.8) scale({sx} {sy})")
        for path in paths:
            path.set("fill", "url(#wordmark-metal)")
            group.append(path)


def add_engine(root, x, y, scale=1):
    # Outlined strokes avoid font dependencies in the engine SVG renderer.
    group = ET.SubElement(
        root,
        tag("g"),
        transform=f"translate({x} {y}) scale({scale})",
        stroke="#b4d9ed",
        **{"stroke-width": "1.5", "stroke-linecap": "square", "fill": "none"},
    )
    glyphs = (
        "M8 0H0V12H8M0 6H6",
        "M0 12V0L8 12V0",
        "M8 2V0H0V12H8V6H4",
        "M0 0H8M4 0V12M0 12H8",
        "M0 12V0L8 12V0",
        "M8 0H0V12H8M0 6H6",
    )
    for i, path in enumerate(glyphs):
        ET.SubElement(group, tag("path"), d=path, transform=f"translate({i * 18} 0)")


def export_vectors():
    identity = svg(512, 154, "0 0 600 180")
    add_identity(identity)
    write("misc/logo/logo.svg", identity)
    write("misc/logo/logo_outlined.svg", identity)
    write("misc/dist/html/logo.svg", identity)

    mark = svg(256, 256, "-5 0 180 180")
    add_identity(mark, wordmark=False)
    write("misc/logo/uzu/mark.svg", mark)
    mark.set("width", "64")
    mark.set("height", "64")
    mark.insert(0, ET.Element(tag("rect"), x="-5", y="0", width="180", height="180", rx="90", fill="#0a1423"))
    write("editor/icons/GodotFile.svg", mark)

    wordmark = svg(187, 69, "180 0 420 180")
    ET.SubElement(wordmark, tag("rect"), x="180", y="0", width="420", height="180", rx="14", fill="#0a1423")
    add_identity(wordmark, mark=False)
    add_engine(wordmark, 187, 157)
    write("editor/icons/Logo.svg", wordmark)

    titlebar = copy.deepcopy(identity)
    titlebar.set("width", "100")
    titlebar.set("height", "24")
    titlebar.insert(0, ET.Element(tag("rect"), width="600", height="180", rx="14", fill="#0a1423"))
    write("editor/icons/TitleBarLogo.svg", titlebar)

    icon = ET.parse(HERE / "official-icon.svg").getroot()
    icon.set("width", "256")
    icon.set("height", "256")
    write("misc/logo/icon.svg", icon)
    write("misc/logo/icon_outlined.svg", icon)
    icon.set("width", "128")
    icon.set("height", "128")
    write("editor/icons/DefaultProjectIcon.svg", icon)
    icon.set("width", "16")
    icon.set("height", "16")
    write("editor/icons/Godot.svg", icon)

    mono = copy.deepcopy(icon)
    mono.remove(mono.find(tag("rect")))
    for path in mono.findall(tag("path")):
        if path.get("stroke"):
            path.set("stroke", "#e0e0e0")
        if path.get("fill") != "none":
            path.set("fill", "#e0e0e0")
    write("editor/icons/GodotMonochrome.svg", mono)

    splash = svg(800, 600, "0 0 800 600")
    group = ET.SubElement(splash, tag("g"), transform="translate(100 190)")
    add_identity(group)
    add_engine(splash, 286, 380, 1.5)
    write("misc/logo/uzu/splash.svg", splash)


def export_rasters():
    from playwright.sync_api import sync_playwright

    # Rendering/conversion only: the logo's shapes and colors come from SVGs.
    outputs = (
        ("misc/logo/icon.svg", "main/app_icon.png", 128, 128),
        ("misc/logo/icon.svg", "misc/logo/icon.png", 256, 256),
        ("misc/logo/icon.svg", "misc/logo/icon_outlined.png", 248, 235),
        ("misc/logo/logo.svg", "misc/logo/logo.png", 512, 207),
        ("misc/logo/logo.svg", "misc/logo/logo_outlined.png", 476, 170),
        ("misc/logo/uzu/splash.svg", "main/splash.png", 800, 600),
        ("misc/logo/uzu/splash.svg", "main/splash_editor.png", 800, 600),
    )
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path="/usr/bin/chromium", args=["--no-sandbox"])
        for source, destination, width, height in outputs:
            page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
            artwork = (ROOT / source).read_text(encoding="utf-8")
            page.set_content(
                f"<style>html,body{{margin:0;background:transparent;width:100%;height:100%}}"
                f"svg{{width:100%;height:100%;display:block}}</style>{artwork}"
            )
            page.screenshot(path=str(ROOT / destination), omit_background=True)
            page.close()
        browser.close()


def export_preview():
    identity = ET.parse(ROOT / "misc/logo/logo.svg").getroot()
    identity.set("viewBox", "0 0 600 220")
    identity.set("role", "img")
    identity.set("aria-label", "U.ZU Engine")
    orbit = identity.find(tag("g"))
    center = identity.find(tag("circle"))
    identity.remove(orbit)
    identity.remove(center)
    animated = ET.Element(tag("g"), id="uzu-orbit")
    animated.extend((orbit, center))
    identity.append(animated)
    add_engine(identity, 187, 178, 1.5)
    template = (HERE / "preview-template.html").read_text(encoding="utf-8")
    (HERE / "preview.html").write_text(
        template.replace("<!-- UZU_IDENTITY -->", ET.tostring(identity, encoding="unicode")), encoding="utf-8"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raster", action="store_true")
    options = parser.parse_args()
    export_vectors()
    export_preview()
    if options.raster:
        export_rasters()
