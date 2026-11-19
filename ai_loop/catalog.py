#!/usr/bin/env python3
"""Search the U.ZU feature catalog (BACKLOG B-403).

The catalog (ai_loop/catalog.json) is generated from the tools' own
definitions by ai_loop/make_catalog.py: every uzu.py command, MCP tool,
preset, tool script and UZU* engine class with its name, description,
tags and place of implementation. Agents search it to discover what
exists instead of reading every document.

Usage: ai_loop/uzu.py catalog <word> [<word> ...] [--limit N]
       (also exposed as the MCP tool "catalog_search")
Every word must match (AND); a word matches when it appears in an
entry's name, description or tags, case-insensitively. Prints one JSON
line {"ok": bool, ...}; a search with no hits is still ok.
Standard library only.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG_PATH = os.path.join(HERE, "catalog.json")


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


def main(argv):
    words, limit = [], 50
    i = 0
    while i < len(argv):
        if argv[i] in ("--query", "--limit") and i + 1 < len(argv):
            if argv[i] == "--query":
                words.append(argv[i + 1])
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
        catalog = load_catalog()
    except (OSError, ValueError) as e:
        print(json.dumps({"ok": False, "error": f"cannot read ai_loop/catalog.json ({e}); regenerate it with ai_loop/make_catalog.py"}))
        return 1
    hits = search(catalog.get("entries", []), query)
    print(json.dumps({"ok": True, "query": query, "count": len(hits), "matches": hits[:limit]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
