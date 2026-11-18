#!/usr/bin/env python3
"""Compare the unit test case lists of two builds (e.g. Windows and the cloud's Linux).

  python ai_loop/test_cases_diff.py list <godot-binary> > cases.txt      # this build's test case names
  python ai_loop/test_cases_diff.py diff a.txt b.txt                      # names only in a / only in b

The list is "--test --list-test-cases" without doctest's own lines, sorted, with duplicates kept
(some names are used by two test cases). Prints one JSON line for diff.
"""
import collections
import json
import subprocess
import sys


def read_list(path):
    with open(path, encoding="utf-8") as f:
        return [l.rstrip("\r\n") for l in f if l.strip()]


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "list":
        r = subprocess.run([sys.argv[2], "--test", "--headless", "--list-test-cases"], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        names = [l.strip() for l in r.stdout.splitlines() if l.strip() and not l.startswith(("[doctest]", "====="))]
        sys.stdout.write("\n".join(sorted(names)) + "\n")
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "diff":
        a, b = collections.Counter(read_list(sys.argv[2])), collections.Counter(read_list(sys.argv[3]))
        print(json.dumps({"a": sum(a.values()), "b": sum(b.values()), "only_in_a": sorted((a - b).elements()),
                          "only_in_b": sorted((b - a).elements())}, ensure_ascii=False))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
