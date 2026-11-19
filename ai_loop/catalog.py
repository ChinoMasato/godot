#!/usr/bin/env python3
"""Search the U.ZU feature catalog (BACKLOG B-403).

The catalog (ai_loop/catalog.json) is generated from the tools' own
definitions by ai_loop/make_catalog.py: every uzu.py command, MCP tool,
preset, tool script and UZU* engine class with its name, description,
tags and place of implementation. Agents search it to discover what
exists instead of reading every document.

Usage: ai_loop/uzu.py catalog <word> [<word> ...] [--limit N]
       [--caps image_gen,gpu] [--profile caps.json]
       (also exposed as the MCP tool "catalog_search")
Every word must match (AND); a word matches when it appears in an
entry's name, description or tags, case-insensitively. Prints one JSON
line {"ok": bool, ...}; a search with no hits is still ok.

With a capability profile (BACKLOG B-404: --caps, --profile, or the
UZU_AGENT_CAPS / UZU_AGENT_PROFILE environment), matches are annotated
instead of filtered out: an entry the profile cannot use gains a
"missing_capabilities" list, and its "fallback" says what to use
instead. Without any profile source, the output is unchanged.
Standard library only.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG_PATH = os.path.join(HERE, "catalog.json")
sys.path.insert(0, HERE)

import agent_caps  # noqa: E402


def load_catalog(path=None):
    with open(path or CATALOG_PATH, encoding="utf-8") as f:
        return json.load(f)


def search(entries, query):
    """Entries whose name, description or tags contain every query word."""
    words = [w.lower() for w in query.split() if w]
    if not words:
        return []
    hits = []
    for entry in entries:
        haystack = " ".join([entry.get("name", ""), entry.get("description", "")] + list(entry.get("tags", []))).lower()
        if all(w in haystack for w in words):
            hits.append(entry)
    return hits


def annotate(entries, profile):
    """Copies of the entries, each unusable one marked with what it is missing."""
    out = []
    for entry in entries:
        missing = agent_caps.missing_capabilities(entry.get("required_capabilities", []), profile)
        out.append({**entry, "missing_capabilities": missing} if missing else entry)
    return out


def main(argv):
    words, limit = [], 50
    caps_arg, profile_arg = None, None
    i = 0
    while i < len(argv):
        if argv[i] in ("--query", "--limit", "--caps", "--profile") and i + 1 < len(argv):
            if argv[i] == "--query":
                words.append(argv[i + 1])
            elif argv[i] == "--caps":
                caps_arg = argv[i + 1]
            elif argv[i] == "--profile":
                profile_arg = argv[i + 1]
            else:
                try:
                    limit = int(argv[i + 1])
                except ValueError:
                    print(json.dumps({"ok": False, "error": f"--limit needs a number, got {argv[i + 1]!r}"}))
                    return 2
            i += 2
        else:
            words.append(argv[i])
            i += 1
    query = " ".join(words).strip()
    if not query:
        print(json.dumps({"ok": False, "error": "catalog needs a word to search for, e.g. uzu.py catalog juice"}))
        return 2
    try:
        profile, sources = agent_caps.load_profile(caps_arg, profile_arg)
    except agent_caps.CapsError as e:
        print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
        return 2
    try:
        catalog = load_catalog()
    except (OSError, ValueError) as e:
        print(json.dumps({"ok": False, "error": f"cannot read ai_loop/catalog.json ({e}); regenerate it with ai_loop/make_catalog.py"}))
        return 1
    hits = search(catalog.get("entries", []), query)
    result = {"ok": True, "query": query, "count": len(hits)}
    if sources:
        result["profile"] = sorted(profile)
        result["profile_sources"] = sources
        hits = annotate(hits, profile)
    result["matches"] = hits[:limit]
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
