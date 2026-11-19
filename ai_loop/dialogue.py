"""Validate and build declarative dialogue and quest-flag data (B-406)."""

import argparse
import json
import os
import tempfile
from collections import deque
from pathlib import Path

STATE_LIMIT = 16_384


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"invalid JSON constant: {value}")


def validate(spec, max_chars):
    issues = []
    text_lengths = {}

    def issue(code, where, message, **details):
        issues.append({"code": code, "where": where, "message": message, **details})

    def report(reachable=(), states=0):
        return {
            "ok": not issues,
            "issues": issues,
            "reachable_nodes": sorted(reachable),
            "states_checked": states,
            "text_lengths": text_lengths,
        }

    def fields(value, where, allowed):
        if not isinstance(value, dict):
            issue("invalid_object", where, "Expected an object.")
            return False
        for key in sorted(value.keys() - allowed):
            issue("unknown_field", f"{where}.{key}", "Unknown field.")
        return True

    def display_text(value, where):
        if not isinstance(value, str) or not value.strip():
            issue("invalid_text", where, "Expected non-empty display text.")
            return
        length = len(value.replace("\n", "").replace("\r", ""))
        text_lengths[where] = length
        if length > max_chars:
            issue("text_too_long", where, "Display text exceeds the character budget.", length=length, limit=max_chars)

    if not fields(spec, "graph", {"entry", "flags", "nodes"}):
        return report()
    flags = spec.get("flags", {})
    nodes = spec.get("nodes")
    entry = spec.get("entry")
    if not isinstance(flags, dict):
        issue("invalid_flags", "flags", "Expected an object mapping flag names to true, false or null.")
    if not isinstance(nodes, dict) or not nodes:
        issue("invalid_nodes", "nodes", "Expected a non-empty object of dialogue nodes.")
    if issues:
        return report()
    if not isinstance(entry, str) or entry not in nodes:
        issue("invalid_entry", "entry", "Expected an existing node ID.")
    for name, initial in sorted(flags.items()):
        if not name.strip() or (initial is not None and type(initial) is not bool):
            issue("invalid_flag", f"flags.{name}", "Expected a non-empty flag name and true, false or null.")

    def check_flags(values, where):
        if not isinstance(values, dict):
            issue("invalid_flags", where, "Expected a flag-to-boolean object.")
            return
        for name, value in sorted(values.items()):
            if name not in flags:
                issue("undeclared_flag", f"{where}.{name}", "Flag is not declared.")
            if type(value) is not bool:
                issue("invalid_flag_value", f"{where}.{name}", "Expected true or false.")

    for name, node in sorted(nodes.items()):
        where = f"nodes.{name}"
        if not name.strip():
            issue("invalid_node_id", where, "Expected a non-empty node ID.")
        if not fields(node, where, {"speaker", "text", "set", "choices", "end"}):
            continue
        display_text(node.get("text"), f"{where}.text")
        if "speaker" in node:
            display_text(node["speaker"], f"{where}.speaker")
        check_flags(node.get("set", {}), f"{where}.set")
        if type(node.get("end", False)) is not bool:
            issue("invalid_end", f"{where}.end", "Expected true or false.")
        choices = node.get("choices", [])
        if not isinstance(choices, list):
            issue("invalid_choices", f"{where}.choices", "Expected an array of choices.")
            continue
        if node.get("end") is True and choices:
            issue("end_has_choices", f"{where}.choices", "An ending may not have outgoing choices.")
        for index, choice in enumerate(choices):
            path = f"{where}.choices[{index}]"
            if not fields(choice, path, {"text", "to", "when"}):
                continue
            display_text(choice.get("text"), f"{path}.text")
            target = choice.get("to")
            if not isinstance(target, str) or target not in nodes:
                issue("unknown_target", f"{path}.to", "Expected an existing node ID.")
            check_flags(choice.get("when", {}), f"{path}.when")

    # Text budgets are independent of graph shape; still report reachability when only text is long.
    if any(item["code"] != "text_too_long" for item in issues):
        return report()

    def state_at(name, values):
        return name, tuple(sorted({**values, **nodes[name].get("set", {})}.items()))

    start = state_at(entry, flags)
    seen = {start}
    queue = deque([start])
    reached = set()
    checked = 0
    reported = set()
    while queue:
        name, state = queue.popleft()
        checked += 1
        reached.add(name)
        node = nodes[name]
        if node.get("end"):
            continue
        values = dict(state)
        available = False
        for index, choice in enumerate(node.get("choices", [])):
            conditions = choice.get("when", {})
            for flag in sorted(conditions):
                path = f"nodes.{name}.choices[{index}].when.{flag}"
                if values[flag] is None and path not in reported:
                    issue(
                        "uninitialized_flag", path, "A reachable path reads this flag before it is set.", flags=values
                    )
                    reported.add(path)
            if not all(values[flag] is required for flag, required in conditions.items()):
                continue
            available = True
            next_state = state_at(choice["to"], values)
            if next_state not in seen:
                if len(seen) >= STATE_LIMIT:
                    issue(
                        "state_limit",
                        f"nodes.{name}",
                        "Too many reachable flag states to verify this graph.",
                        limit=STATE_LIMIT,
                    )
                    return report(reached, checked)
                seen.add(next_state)
                queue.append(next_state)
        path = f"nodes.{name}.choices"
        if not available and path not in reported:
            issue("branch_gap", path, "No choice is available in this reachable state.", flags=values)
            reported.add(path)
    for name in sorted(nodes.keys() - reached):
        issue("unreachable_node", f"nodes.{name}", "No path from the entry reaches this node.")
    return report(reached, checked)


def write_graph(path, spec):
    # Keep any previous output if serialization or replacement fails.
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as file:
            temp_path = Path(file.name)
            json.dump(spec, file, ensure_ascii=False, indent=2)
            file.write("\n")
        os.replace(temp_path, path)
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--out", type=Path, help="Write a validated JSON graph to this path")
    parser.add_argument("--max-chars", type=int, default=160, help="Maximum characters per displayed field")
    args = parser.parse_args(argv)
    try:
        if args.max_chars < 1:
            raise ValueError("--max-chars must be positive")
        with args.spec.open(encoding="utf-8-sig") as file:
            spec = json.load(file, object_pairs_hook=unique_object, parse_constant=reject_constant)
        result = validate(spec, args.max_chars)
        if result["ok"] and args.out is not None:
            if args.out.resolve() == args.spec.resolve():
                raise ValueError("--out must differ from --spec")
            write_graph(args.out, spec)
            result["out"] = str(args.out)
        print(json.dumps(result))
        return 0 if result["ok"] else 1
    except (OSError, ValueError, RecursionError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 2
