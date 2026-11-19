#!/usr/bin/env python3
"""Pick how a texture is made, by agent capability (BACKLOG B-404).

The same goal — a PBR texture set for a bare model — has two routes, and
which one works depends on the calling agent:

  * the profile declares image_gen (e.g. Codex): the agent generates an
    image, and UZUPBRFromImage (B-902) turns it into albedo / normal /
    ORM maps:  uzu.py picture <project> pbrmake make --in <image>
    --out res://materials/<name>
  * it does not (e.g. Claude Code): the procedural PBR presets (B-1209)
    make a seamless base texture from noise and patterns, no image
    input needed:  uzu.py look <project> pbr --assign 'House*:plaster'

The result always says which route was picked and why (the capability
that decided it), so a missing capability comes back as a structured
fallback instead of a failure. Declare the profile with --caps /
--profile or the environment (see ai_loop/agent_caps.py).

Usage: ai_loop/uzu.py texture-route [--caps image_gen] [--profile caps.json]
       [--image generated.png] [--preset wood]
Prints one JSON line {"ok": bool, ...}. Standard library only.
"""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import agent_caps  # noqa: E402

# Catalog declaration (BACKLOG B-404), read by ai_loop/make_catalog.py:
# this tool needs no capability itself — it adapts to the profile.
CAPABILITIES = {
    "required_capabilities": [],
    "fallback": "Without image_gen, texture-route picks the procedural PBR presets (B-1209) instead of a generated image (B-902).",
}


def pbr_presets():
    """The procedural preset names, read from their definition in engine_tool.gd."""
    with open(os.path.join(HERE, "engine_tool.gd"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"const PBR_PRESETS := \{(.*?)\n\}", text, re.S)
    if not m:
        return []
    return sorted(set(re.findall(r'"([a-z][a-z0-9-]*)"\s*:', m.group(1))))


def route(profile, image=None, preset=None):
    """The routing decision for a capability profile, as a result dict."""
    profile = frozenset(profile)
    if "image_gen" in profile:
        result = {
            "ok": True,
            "command": "texture-route",
            "route": "generated_image",
            "reason": "the agent profile declares image_gen, so a generated image is turned into albedo/normal/ORM maps by UZUPBRFromImage (B-902)",
            "profile": sorted(profile),
            "capability": "image_gen",
        }
        if not image:
            result.update({
                "ok": False,
                "error": "the generated-image route needs an image: generate one with the agent's image tool, then pass --image <path>",
                "needs_input": "image",
                "fallback": "no image yet? the procedural route needs no input: use a profile without image_gen, or pass --preset <name> (B-1209)",
            })
        elif not os.path.isfile(image):
            result.update({
                "ok": False,
                "error": f"image not found: {image}",
                "needs_input": "image",
                "fallback": "check the path, or use the procedural route (a profile without image_gen, or --preset <name>)",
            })
        else:
            result["image"] = os.path.abspath(image)
            result["next_steps"] = [
                "copy the image into the project (e.g. res://textures/) and import it: uzu.py engine <project> import",
                "make the PBR maps: uzu.py picture <project> pbrmake make --in res://textures/<image> --out res://materials/<name>",
                "assign them like any PBR material set (the maps land in the --out folder as albedo/normal/ORM)",
            ]
        return result
    presets = pbr_presets()
    result = {
        "ok": True,
        "command": "texture-route",
        "route": "procedural_preset",
        "reason": "the agent profile does not declare image_gen, so the procedural PBR presets (B-1209) are used; they need no image input",
        "profile": sorted(profile),
        "capability": "image_gen",
        "presets": presets,
    }
    if preset:
        if preset not in presets:
            result.update({"ok": False, "error": f"unknown preset {preset!r}: one of {', '.join(presets)}"})
        else:
            result["preset"] = preset
            result["next_steps"] = [
                f"make and assign in one call: uzu.py look <project> pbr --assign 'NodePattern:{preset}' (add --scene res://x.tscn to name the scene)",
                f"or make the maps only: engine command pbr-preset --name {preset} --out res://materials/pbr, then import the project",
            ]
    return result


def main(argv):
    opts = {}
    i = 0
    while i < len(argv):
        if argv[i] in ("--caps", "--profile", "--image", "--preset") and i + 1 < len(argv):
            opts[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            print(json.dumps({"ok": False, "error": f"texture-route: unexpected argument {argv[i]!r}"}))
            return 2
    try:
        profile, _sources = agent_caps.load_profile(opts.get("caps"), opts.get("profile"))
    except agent_caps.CapsError as e:
        print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
        return 2
    result = route(profile, opts.get("image"), opts.get("preset"))
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
