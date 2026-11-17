#!/usr/bin/env python3
"""MCP server for U.ZU Engine (BACKLOG B-402): engine and look operations as tools.

Speaks the Model Context Protocol over stdio (JSON-RPC 2.0, one message per
line) and runs ai_loop/uzu.py (engine and look) for each tool call, so an
MCP client (for example Claude Code) can operate a project directly.

Usage: ai_loop/mcp_server.py --project <project-dir>
       ai_loop/mcp_server.py --self-test

Register with Claude Code, for example:
  claude mcp add uzu-engine -- python3 /path/to/ai_loop/mcp_server.py --project /path/to/game
Standard library only.
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROTOCOL_VERSION = "2025-06-18"


def _str(description):
    return {"type": "string", "description": description}


SCENE = _str("Scene path, e.g. res://main.tscn")
NODE = _str('Node path relative to the scene root ("." is the root)')

# name: (script, command, description, properties, required)
TOOLS = {
    "scene_new": ("engine", "new", "Create a new scene file with a root node.", {"scene": SCENE, "type": _str("Root node class, e.g. Node3D"), "name": _str("Root node name")}, ["scene"]),
    "scene_tree": ("engine", "tree", "List the nodes of a scene with their types and scripts.", {"scene": SCENE, "depth": _str("Maximum depth (optional)")}, ["scene"]),
    "node_props": ("engine", "props", "List a node's inspector properties and their values.", {"scene": SCENE, "node": NODE}, ["scene", "node"]),
    "node_get": ("engine", "get", "Read a node property (supports indexed paths like position:y).", {"scene": SCENE, "node": NODE, "property": _str("Property name")}, ["scene", "node", "property"]),
    "node_set": ("engine", "set", 'Set a node property and save. Values use Godot text format: 1.5, "text", Vector3(1, 2, 3); res:// paths load resources; resource class names (BoxMesh) create one.', {"scene": SCENE, "node": NODE, "property": _str("Property name"), "value": _str("Value in Godot text format")}, ["scene", "node", "property", "value"]),
    "node_add": ("engine", "add", "Add a node of a class under a parent and save; 'set' is a JSON object of initial property values.", {"scene": SCENE, "parent": NODE, "type": _str("Node class"), "name": _str("Node name"), "set": _str('JSON object, e.g. {"mesh": "BoxMesh", "position": "Vector3(0, 1, 0)"}')}, ["scene", "type"]),
    "node_instance": ("engine", "instance", "Instance another scene under a parent and save.", {"scene": SCENE, "parent": NODE, "path": _str("Scene to instance, res://..."), "name": _str("Node name")}, ["scene", "path"]),
    "node_remove": ("engine", "remove", "Remove a node (not the root) and save.", {"scene": SCENE, "node": NODE}, ["scene", "node"]),
    "node_reparent": ("engine", "reparent", "Move a node under another parent and save.", {"scene": SCENE, "node": NODE, "parent": NODE}, ["scene", "node", "parent"]),
    "node_call": ("engine", "call", "Call a method on a node and return the result; 'args' is an array in Godot text format.", {"scene": SCENE, "node": NODE, "method": _str("Method name"), "args": _str('e.g. [1, "a"]')}, ["scene", "node", "method"]),
    "signal_connect": ("engine", "connect", "Connect a signal to a method persistently and save.", {"scene": SCENE, "from": NODE, "signal": _str("Signal name"), "to": NODE, "method": _str("Method name")}, ["scene", "from", "signal", "to", "method"]),
    "script_attach": ("engine", "script", "Attach a script to a node and save.", {"scene": SCENE, "node": NODE, "path": _str("Script path, res://...")}, ["scene", "node", "path"]),
    "script_check": ("engine", "check", "Check a script; returns errors and warnings with file, line, column, code and message.", {"path": _str("Script path, res://...")}, ["path"]),
    "class_list": ("engine", "classes", "List engine classes, optionally only those inheriting a class.", {"inherits": _str("Base class (optional)")}, []),
    "class_describe": ("engine", "describe", "Describe a class: properties, methods, signals and constants.", {"class": _str("Class name")}, ["class"]),
    "project_run": ("engine", "run", "Run the project headless for some frames; returns errors and the log tail.", {"frames": _str("Frames to run (default 120)")}, []),
    "input_actions": ("engine", "actions", "List the project's input actions (InputMap) and their key/button events.", {}, []),
 "resource_new": ("engine", "res-new", "Create a resource file (.tres) of a class, with optional initial values.", {"path": _str("res://path.tres"), "type": _str("Resource class, e.g. StandardMaterial3D"), "set": _str('JSON object of values in Godot text format')}, ["path", "type"]),
    "resource_props": ("engine", "res-props", "List a resource file's inspector properties and values.", {"path": _str("res://path.tres")}, ["path"]),
    "resource_get": ("engine", "res-get", "Read a property of a resource file.", {"path": _str("res://path.tres"), "property": _str("Property name")}, ["path", "property"]),
    "resource_set": ("engine", "res-set", "Set a property of a resource file and save.", {"path": _str("res://path.tres"), "property": _str("Property name"), "value": _str("Value in Godot text format")}, ["path", "property", "value"]),
    "setting_get": ("engine", "setting-get", "Read a project setting.", {"name": _str("Setting path, e.g. application/config/name")}, ["name"]),
    "setting_set": ("engine", "setting-set", "Change a project setting and save project.godot.", {"name": _str("Setting path"), "value": _str("Value in Godot text format")}, ["name", "value"]),
    "action_add": ("engine", "action-add", "Add an input action or add events to it (key, mouse button, joypad button).", {"name": _str("Action name"), "key": _str("Key name, e.g. Space"), "mouse": _str("left | right | middle | wheel_up | wheel_down"), "joy-button": _str("Joypad button index"), "deadzone": _str("Deadzone"), "replace": {"type": "boolean", "description": "Replace the existing events"}}, ["name"]),
    "action_remove": ("engine", "action-remove", "Remove an input action.", {"name": _str("Action name")}, ["name"]),
    "assets_import": ("engine", "import", "Import (or reimport) the project's assets: textures, sounds, models.", {}, []),
    "import_get": ("engine", "import-get", "Read an asset's import settings.", {"path": _str("res://asset path")}, ["path"]),
    "import_set": ("engine", "import-set", "Change one import setting of an asset and reimport it.", {"path": _str("res://asset path"), "param": _str("Import parameter, e.g. compress/mode"), "value": _str("Value")}, ["path", "param", "value"]),
    "export_preset": ("engine", "export-preset", "Create or update an export preset (export_presets.cfg).", {"platform": _str("web | linux | windows | macos | android"), "name": _str("Preset name"), "out": _str("Export path, relative to the project"), "template": _str("Custom export template zip (absolute path)")}, ["platform"]),
    "export_presets": ("engine", "presets", "List the project's export presets.", {}, []),
    "export_templates": ("engine", "templates", "Where export templates are looked for, which are installed, and the presets' custom templates.", {}, []),
    "project_export": ("engine", "export", "Export the project with a preset; lists the files written.", {"preset": _str("Preset name"), "out": _str("Output file (default: the preset's path)")}, ["preset"]),
    "project_validate": ("engine", "validate", "Load every scene, resource and script of the project; list problems (missing files, broken scripts with errors, missing main scene or autoloads).", {}, []),
    "screenshot": ("look", "shot", "Render the main scene without a GPU and save a PNG.", {"out": _str("Output PNG path"), "frames": _str("Frames before capture (default 8)")}, ["out"]),
    "panorama": ("look", "panorama", "Capture a 360-degree equirectangular panorama around the main camera and save a PNG.", {"out": _str("Output PNG path"), "width": _str("Width in pixels (default 1024)")}, ["out"]),
    "image_check": ("look", "check", "Check a screenshot: color balance, muddiness, detail by height and composition.", {"image": _str("PNG path")}, ["image"]),
    "look_apply": ("look", "apply", "Apply the U.ZU look (sky, light, aerial perspective, color grade) and configure the sun.", {"scene": SCENE, "quality": _str("high | standard | low"), "atmosphere": _str("clear | humid | golden_hour | night"), "clouds": {"type": "boolean", "description": "Sky with clouds"}}, ["scene"]),
    "look_hd2d": ("look", "hd2d", "Apply the HD-2D preset in one go: the look, tilt-shift blur, lit outlined pixel-art sprites with blob shadows, one texel size, and a telephoto camera looking down.", {"scene": SCENE, "atmosphere": _str("clear | humid | golden_hour | night (default golden_hour)"), "no-camera": {"type": "boolean", "description": "Leave the camera as it is"}}, ["scene"]),
    "look_rig": ("look", "rig", "Add fill and rim lights (three-point lighting with the sun as key).", {"scene": SCENE, "atmosphere": _str("clear | humid | golden_hour | night")}, ["scene"]),
    "look_lamp": ("look", "lamp", "Add a warm lamp with a glowing bulb.", {"scene": SCENE, "position": _str("x,y,z"), "quality": _str("high | standard | low")}, ["scene", "position"]),
    "look_camera": ("look", "camera", "Set a camera's lens, eye height and vertical shift.", {"scene": SCENE, "node": NODE, "lens": _str("wide | standard | telephoto"), "eye": _str("standing | low"), "shift": _str("Fraction of view height, positive is up")}, ["scene"]),
    "lightmap_bake": ("look", "bake", "Bake the scene's LightmapGI (runs the editor with a renderer; can take minutes). 'prepare' turns on UV2 for primitive meshes.", {"scene": SCENE, "node": NODE, "prepare": {"type": "boolean", "description": "Turn on UV2 for primitive meshes"}}, ["scene"]),
    "look_sprite": ("look", "sprite", "Make Sprite3D nodes lit pixel-art sprites (light, shadows, fog; crisp texels), with optional normal map, outline and blob shadow.", {"scene": SCENE, "node": NODE, "normal": _str("Normal map, res://..."), "outline": _str("Outline width in texels (0-4)"), "blob": _str("Blob shadow radius (0 = none)")}, ["scene"]),
    "look_texels": ("look", "texels", "Check the texel density of the scene's sprites: warns on sprites whose pixels are bigger or smaller than the others', and on blurry filtering.", {"scene": SCENE, "tolerance": _str("Allowed difference, default 0.05")}, ["scene"]),
    "look_pixel": ("look", "pixel", "Pixel discipline preset: one texel size for all sprites, nearest filtering, optional snapping to the texel grid.", {"scene": SCENE, "texel": _str("Texel size in meters (default: the sprites' median)"), "snap": {"type": "boolean", "description": "Snap sprite positions to the texel grid"}}, ["scene"]),
    "look_material": ("look", "material", "Create a preset material (save to 'out', or assign to 'node' in 'scene').", {"kind": _str("skin | foliage | glass | water | lamp"), "out": _str("res://path.tres"), "scene": SCENE, "node": NODE}, ["kind"]),
}


def call_tool(project, name, arguments):
    script, command, _, _, _ = TOOLS[name]
    argv = [sys.executable, os.path.join(HERE, "uzu.py"), script, project, command]  # Cross-platform entry (B-801).
    for key, value in (arguments or {}).items():
        if value is None or value == "" or value is False:
            continue
        argv += [f"--{key}"] if value is True else [f"--{key}", str(value)]
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=600)
    lines = [l for l in proc.stdout.splitlines() if l.startswith("{")]
    text = lines[-1] if lines else json.dumps({"ok": False, "error": (proc.stderr or proc.stdout)[-2000:]})
    try:
        ok = json.loads(text).get("ok", False)
    except ValueError:
        ok = False
    return {"content": [{"type": "text", "text": text}], "isError": not ok}


def handle(project, message):
    method, mid = message.get("method"), message.get("id")
    if method == "initialize":
        result = {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}}, "serverInfo": {"name": "uzu-engine", "version": "0.1"}}
    elif method == "tools/list":
        result = {"tools": [{"name": n, "description": d, "inputSchema": {"type": "object", "properties": p, "required": r}} for n, (_, _, d, p, r) in TOOLS.items()]}
    elif method == "tools/call":
        params = message.get("params", {})
        if params.get("name") not in TOOLS:
            return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": f"unknown tool {params.get('name')}"}}
        result = call_tool(project, params["name"], params.get("arguments"))
    elif method == "ping":
        result = {}
    elif mid is None:
        return None  # Notifications (e.g. notifications/initialized) get no response.
    else:
        return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"method not found: {method}"}}
    return {"jsonrpc": "2.0", "id": mid, "result": result}


def serve(project):
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            response = handle(project, json.loads(line))
        except Exception as e:  # Keep serving; report the failure to the client.
            response = {"jsonrpc": "2.0", "id": None, "error": {"code": -32603, "message": str(e)}}
        if response is not None:
            sys.stdout.write(json.dumps(response) + "\n")
            sys.stdout.flush()


def self_test():
    with tempfile.TemporaryDirectory() as project:
        with open(os.path.join(project, "project.godot"), "w") as f:
            f.write('config_version=5\n\n[application]\n\nconfig/name="mcp selftest"\nrun/main_scene="res://main.tscn"\n')
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": PROTOCOL_VERSION, "capabilities": {}, "clientInfo": {"name": "selftest", "version": "0"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "scene_new", "arguments": {"scene": "res://main.tscn", "type": "Node3D"}}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "node_add", "arguments": {"scene": "res://main.tscn", "parent": ".", "type": "OmniLight3D", "name": "Light"}}},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "look_apply", "arguments": {"scene": "res://main.tscn", "atmosphere": "humid"}}},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "scene_tree", "arguments": {"scene": "res://main.tscn"}}},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "node_get", "arguments": {"scene": "res://main.tscn", "node": "Nope", "property": "position"}}},
            {"jsonrpc": "2.0", "id": 8, "method": "nope"},
        ]
        proc = subprocess.run([sys.executable, os.path.abspath(__file__), "--project", project], input="\n".join(json.dumps(r) for r in requests) + "\n", capture_output=True, text=True, timeout=600)
        responses = {r["id"]: r for r in map(json.loads, proc.stdout.splitlines())}
        assert responses[1]["result"]["serverInfo"]["name"] == "uzu-engine", responses[1]
        assert len(responses[2]["result"]["tools"]) == len(TOOLS), responses[2]
        for i in (3, 4, 5, 6):
            assert not responses[i]["result"]["isError"], responses[i]
        tree = json.loads(responses[6]["result"]["content"][0]["text"])
        paths = [n["path"] for n in tree["nodes"]]
        assert "Light" in paths and "WorldEnvironment" in paths and "Sun" in paths, tree
        assert responses[7]["result"]["isError"], responses[7]
        assert responses[8]["error"]["code"] == -32601, responses[8]
        assert len(responses) == 8, "notifications must not get a response"
    print("mcp selftest: ok")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    if "--project" not in sys.argv:
        print(__doc__, file=sys.stderr)
        return 2
    serve(os.path.abspath(sys.argv[sys.argv.index("--project") + 1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
