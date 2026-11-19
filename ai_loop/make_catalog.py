#!/usr/bin/env python3
"""Generate the U.ZU feature catalog, ai_loop/catalog.json (BACKLOG B-403).

Every entry is extracted from the definition of the thing it describes,
so the catalog cannot drift from the implementation by hand:

  kind "cli"     the top-level tools of ai_loop/uzu.py (dispatch in main()
                 plus the usage/description in its module docstring)
  kind "command" the subcommands of uzu.py engine / look / map: the ones
                 handled in uzu.py itself (AST of engine()/look()) and the
                 ones of the GDScript tools (the command lists and header
                 comments of engine_tool.gd / look_tool.gd / map_tool.gd)
  kind "mcp"     the tools of ai_loop/mcp_server.py (its TOOLS table)
  kind "preset"  the JSON presets under ai_loop/presets/ (their "_about")
  kind "script"  the tool scripts ai_loop/*.py and ai_loop/*.gd
                 (module docstring / leading ## comment)
  kind "class"   the UZU* engine classes (doc/classes/UZU*.xml and
                 modules/*/doc_classes/UZU*.xml brief descriptions)

Descriptions come from the definitions (docstrings, header comments,
the MCP table); where several sources describe the same command, the
GDScript header wins, then the MCP table. Nothing is written by hand here.

Capability requirements (BACKLOG B-404) are extracted the same way, from
declarations next to the definitions: TOOL_CAPABILITIES in mcp_server.py
(for "mcp" entries, and for a "command" entry when exactly one MCP tool
maps to it), a top-level CAPABILITIES dict literal in a tool script (for
"script" entries), and in the module a top-level uzu.py tool dispatches
to (for "cli" entries, see CLI_MODULES). Every entry carries
"required_capabilities" (possibly empty) and "fallback" (or null); the
names are validated against ai_loop/agent_caps.py.

Usage: ai_loop/make_catalog.py [--check]
  --check  do not write; fail if the committed catalog's entry set
           differs from a fresh extraction (used by the tests).
Prints one JSON line {"ok": bool, ...}. Standard library only.
"""

import ast
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "catalog.json")
sys.path.insert(0, HERE)


def rel(path):
    return os.path.relpath(path, ROOT)


def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------- uzu.py CLI


def _uzu_doc():
    """{tool: (usage, description)} from uzu.py's module docstring."""
    doc = ast.get_docstring(ast.parse(_read(os.path.join(HERE, "uzu.py")))) or ""
    raw = {}
    current = None
    for line in doc.splitlines():
        m = re.match(r"^\s+uzu\.py\s+(\S+)\s*(.*)$", line)
        if m:
            current = m.group(1)
            raw.setdefault(current, []).append(m.group(2))
        elif current and re.match(r"^\s{10,}\S", line):
            raw[current].append(line.strip())
        elif line and not line[0].isspace():
            current = None
    out = {}
    for tool, lines in raw.items():
        usage, desc = [], []
        for i, text in enumerate(lines):
            m = re.search(r"\s{3,}", text)
            if m:
                left, right = text[: m.start()].strip(), text[m.end():].strip()
                if left:
                    usage.append(left)
                if right:
                    desc.append(right)
            elif i == 0 or text.startswith(("[", "--", "<")):
                usage.append(text)
            else:
                desc.append(text)
        out[tool] = (" ".join(usage), " ".join(desc))
    return out


def _uzu_dispatch():
    """The authoritative top-level tool names: what main() dispatches on."""
    tree = ast.parse(_read(os.path.join(HERE, "uzu.py")))
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == "main":
            for sub in ast.walk(node):
                if isinstance(sub, ast.Compare) and isinstance(sub.left, ast.Name) and sub.left.id == "tool":
                    for comp in sub.comparators:
                        if isinstance(comp, ast.Constant) and isinstance(comp.value, str):
                            names.add(comp.value)
                        elif isinstance(comp, ast.Tuple):
                            names.update(e.value for e in comp.elts if isinstance(e, ast.Constant))
    return names


def _uzu_python_commands(function):
    """Subcommand names handled in Python inside uzu.py's engine()/look()."""
    tree = ast.parse(_read(os.path.join(HERE, "uzu.py")))
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == function:
            for sub in ast.walk(node):
                if isinstance(sub, ast.Compare) and isinstance(sub.left, ast.Name) and sub.left.id == "command":
                    names.update(c.value for c in sub.comparators if isinstance(c, ast.Constant) and isinstance(c.value, str))
    return names


# ---------------------------------------------------------------- GDScript tools


def _prose(text):
    """The prose part of a GDScript header line: drop the argument tokens.

    Arguments are --flags and their values, [optional] groups and bracketed
    JSON; prose is the first plain word (or hyphenated / capitalised term)
    outside them, and everything after it.
    """
    out, expect_value, depth = [], False, 0
    for tok in text.split():
        depth += tok.count("[") + tok.count("{") - tok.count("]") - tok.count("}")
        plain = tok.strip("(),")
        if out:
            out.append(tok)
        elif expect_value:
            if depth <= 0:
                expect_value = False
        elif depth > 0:
            pass  # Inside a bracketed argument.
        elif tok.lstrip("[").startswith("--"):
            expect_value = not tok.endswith("]")
        elif tok.startswith("(") or re.match(r"^[a-z][a-z0-9_'/-]*$", plain) or re.match(r"^[A-Za-z][A-Za-z0-9]*-[A-Za-z0-9-]+$", plain) or re.match(r"^[A-Z][A-Z0-9]{1,}$", plain):
            out.append(tok)
    return " ".join(out).strip()


def _gd_header(path):
    """{command: description} from a GDScript tool's leading ## command list."""
    entries, current = {}, None
    for line in _read(path).splitlines()[:130]:
        if not line.startswith("##"):
            continue
        m = re.match(r"^##   ([a-z][a-z0-9-]*)(\s+(.*))?$", line)
        cont = re.match(r"^##\s{4,}(\S.*)$", line)
        if m:
            name = m.group(1)
            current = name if name not in entries else None
            if current:
                entries[name] = [m.group(3) or ""]
        elif cont and current:
            entries[current].append(cont.group(1))
        elif not line.startswith("##  "):
            current = None
    return {name: _prose(" ".join(parts)) for name, parts in entries.items()}


def _gd_match_arms(path):
    """Command names from `match command:` arms:  "name":  /  "a", "b":"""
    names, arm_indent = set(), None
    for line in _read(path).splitlines():
        indent = len(line) - len(line.lstrip())
        if re.match(r"^\s*match\s+command\s*:", line):
            arm_indent = indent
            continue
        if arm_indent is not None:
            if line.strip() and indent <= arm_indent:
                arm_indent = None
            else:
                m = re.match(r'^\s+"([a-z][a-z0-9-]*)"((?:\s*,\s*"[a-z][a-z0-9-]*")*)\s*:', line)
                if m:
                    names.add(m.group(1))
                    names.update(re.findall(r'"([a-z][a-z0-9-]*)"', m.group(2)))
    return names


def _gd_compared(path):
    """Command names from `command == "x"` / `command != "x"` in a GDScript tool."""
    return set(re.findall(r'command\s*[!=]=\s*"([a-z][a-z0-9-]*)"', _read(path)))


def _gd_info_commands(path):
    """The explicit "commands": [...] list a GDScript tool reports about itself."""
    m = re.search(r'"commands":\s*\[([^\]]*)\]', _read(path))
    return set(re.findall(r'"([a-z][a-z0-9-]*)"', m.group(1))) if m else set()


def _gd_summary(path):
    """First prose of a GDScript file's leading ## comment, for script entries."""
    parts = []
    for line in _read(path).splitlines()[:40]:
        if line.startswith("##"):
            text = line[2:].strip()
            if not text:
                break
            parts.append(text)
        elif parts:
            break
    return " ".join(parts)[:240]


# ---------------------------------------------------------------- capabilities


# Top-level uzu.py tools that dispatch to a module of their own: a
# CAPABILITIES declaration in that module describes the "cli" entry.
CLI_MODULES = {"caps": "agent_caps.py", "catalog": "catalog.py", "replay-compare": "replay_compare.py", "texture-route": "texture_route.py"}


def _declared_capabilities(path):
    """A tool module's top-level CAPABILITIES = {...} literal, or None."""
    try:
        tree = ast.parse(_read(path))
    except (OSError, SyntaxError):
        return None
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "CAPABILITIES" for t in node.targets):
            try:
                decl = ast.literal_eval(node.value)
            except (ValueError, SyntaxError):
                raise ValueError(f"{rel(path)}: CAPABILITIES must be a literal dict")
            if not isinstance(decl, dict):
                raise ValueError(f"{rel(path)}: CAPABILITIES must be a dict")
            return decl
    return None


def _apply_capabilities(entries):
    """Fill every entry's required_capabilities / fallback from the declarations."""
    import agent_caps
    import mcp_server

    decls = {}
    tool_caps = getattr(mcp_server, "TOOL_CAPABILITIES", {})
    for name, decl in tool_caps.items():
        decls[("mcp", "", name)] = decl
    by_command = {}
    for name, spec in mcp_server.TOOLS.items():
        decl = tool_caps.get(name)
        if decl and spec[1]:
            by_command.setdefault((spec[0], spec[1]), []).append(decl)
    for (script, command), found in by_command.items():
        if len(found) == 1:  # Only an unambiguous mapping describes a command.
            decls[("command", script, command)] = found[0]
    for entry in entries:
        decl = decls.get((entry["kind"], entry.get("tool", ""), entry["name"]))
        if decl is None and entry["kind"] == "script" and entry["source"].endswith(".py"):
            decl = _declared_capabilities(os.path.join(ROOT, entry["source"]))
        if decl is None and entry["kind"] == "cli" and entry["name"] in CLI_MODULES:
            decl = _declared_capabilities(os.path.join(HERE, CLI_MODULES[entry["name"]]))
        if decl:
            required = agent_caps.validate_names(decl.get("required_capabilities", []), f"capabilities of {entry['kind']} {entry['name']}")
            entry["required_capabilities"] = sorted(set(required))
            entry["fallback"] = decl.get("fallback")


# ---------------------------------------------------------------- builders


def build_catalog():
    entries = []

    def add(name, kind, description, tags, source, **extra):
        entry = {"name": name, "kind": kind, "description": description, "tags": sorted(set(tags)), "source": source, "required_capabilities": [], "fallback": None}
        entry.update(extra)
        entries.append(entry)

    # MCP first: its descriptions double as fallbacks for CLI commands.
    import mcp_server

    mcp_desc = {}
    for name in sorted(mcp_server.TOOLS):
        script, command, description, _, _ = mcp_server.TOOLS[name]
        mcp_desc[(script, command)] = description
        add(name, "mcp", description, ["mcp", script] + name.split("_"), "ai_loop/mcp_server.py", maps_to={"tool": script, "command": command})

    doc = _uzu_doc()
    for tool in sorted(_uzu_dispatch()):
        usage, description = doc.get(tool, ("", ""))
        add(tool, "cli", description, ["cli", "uzu.py"], "ai_loop/uzu.py", usage=f"uzu.py {tool} {usage}".strip())

    mcp_by_command = {}
    for (_, command), description in mcp_desc.items():
        if command:
            mcp_by_command.setdefault(command, description)

    # tool: (GDScript file, commands handled in uzu.py itself, header has a command list)
    gd_tools = {
        "engine": ("engine_tool.gd", _uzu_python_commands("engine"), True),
        "look": ("look_tool.gd", _uzu_python_commands("look"), True),
        "map": ("map_tool.gd", set(), False),  # map_tool.gd's header is prose, not a command list.
    }
    for tool, (gd_file, python_commands, use_header) in gd_tools.items():
        gd_path = os.path.join(HERE, gd_file)
        header = _gd_header(gd_path) if use_header else {}
        gd_names = set(header) | _gd_match_arms(gd_path) | _gd_compared(gd_path) | _gd_info_commands(gd_path)
        for name in sorted(gd_names | python_commands):
            description = header.get(name) or mcp_desc.get((tool, name)) or mcp_by_command.get(name, "")
            source = f"ai_loop/{gd_file}" if name in gd_names else "ai_loop/uzu.py"
            add(name, "command", description, ["cli", tool] + name.split("-"), source, tool=tool, usage=f"uzu.py {tool} <project> {name}")

    for path in sorted(glob.glob(os.path.join(HERE, "presets", "**", "*.json"), recursive=True)):
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        group = os.path.basename(os.path.dirname(path))
        name = os.path.splitext(os.path.basename(path))[0]
        add(name, "preset", data.get("_about", ""), ["preset", group], rel(path), group=group)

    for path in sorted(glob.glob(os.path.join(HERE, "*.py")) + glob.glob(os.path.join(HERE, "*.gd"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if path.endswith(".py"):
            description = (ast.get_docstring(ast.parse(_read(path))) or "").split("\n")[0]
            tags = ["script", "python"]
        else:
            description = _gd_summary(path)
            tags = ["script", "gdscript"]
        add(name, "script", description.strip(), tags, rel(path))

    class_files = sorted(glob.glob(os.path.join(ROOT, "doc", "classes", "UZU*.xml")) + glob.glob(os.path.join(ROOT, "modules", "*", "doc_classes", "UZU*.xml")))
    for path in class_files:
        root_el = ET.parse(path).getroot()
        brief = " ".join((root_el.findtext("brief_description") or "").split())
        add(root_el.get("name"), "class", brief, ["class", "uzu"], rel(path))

    _apply_capabilities(entries)

    order = {"cli": 0, "command": 1, "mcp": 2, "preset": 3, "script": 4, "class": 5}
    entries.sort(key=lambda e: (order[e["kind"]], e.get("tool", ""), e["name"]))
    counts = {}
    for e in entries:
        counts[e["kind"]] = counts.get(e["kind"], 0) + 1
    counts["total"] = len(entries)
    return {"version": 1, "generated_by": "ai_loop/make_catalog.py", "counts": counts, "entries": entries}


def entry_keys(catalog):
    return sorted((e["kind"], e.get("tool", ""), e["name"]) for e in catalog["entries"])


def main(argv):
    try:
        catalog = build_catalog()
    except ValueError as e:
        print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
        return 1
    if "--check" in argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                committed = json.load(f)
        except (OSError, ValueError) as e:
            print(json.dumps({"ok": False, "error": f"cannot read catalog.json: {e}"}))
            return 1
        fresh, old = entry_keys(catalog), entry_keys(committed)
        if fresh != old:
            print(json.dumps({"ok": False, "error": "catalog.json is out of date; run ai_loop/make_catalog.py", "missing": [list(k) for k in set(fresh) - set(old)], "stale": [list(k) for k in set(old) - set(fresh)]}, ensure_ascii=False))
            return 1
        print(json.dumps({"ok": True, "check": "catalog.json matches the definitions", "count": len(fresh)}))
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=1)
        f.write("\n")
    print(json.dumps({"ok": True, "out": rel(OUT), "counts": catalog["counts"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
