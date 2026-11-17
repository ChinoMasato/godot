#!/usr/bin/env python3
"""Pre-publish check for an exported game build (BACKLOG B-502).

Usage:
  ai_loop/prepublish.py check <build-dir | build.zip> [--max-mb 500] [--max-files 1000]
  ai_loop/prepublish.py add-notices <build-dir>    copies the engine's LICENSE.txt and COPYRIGHT.txt in
  ai_loop/prepublish.py --self-test

check prints one JSON line {"ok", "size_mb", "files", "problems": [...], "found": {...}}
and exits 1 when there is a problem:
  - engine_license_missing: no file has the engine's MIT license (its copyright
    line and the permission notice). The MIT license requires shipping it.
  - third_party_notices_missing: no file has the third-party notices (the
    engine's COPYRIGHT.txt), which several bundled libraries require.
  - too_large: the build is bigger than --max-mb (itch.io: 1000 MB per upload;
    browser games are best kept far smaller, hence 500 by default).
  - too_many_files: more files than --max-files (itch.io HTML5: 1000).
Standard library only.
"""

import json
import os
import shutil
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEXT_EXTENSIONS = {"", ".txt", ".md", ".html", ".htm"}
MAX_TEXT_BYTES = 4 * 1024 * 1024


def _engine_copyright_line():
    with open(os.path.join(ROOT, "LICENSE.txt"), encoding="utf-8") as f:
        return f.readline().strip()


ENGINE_COPYRIGHT = _engine_copyright_line()
MIT_PERMISSION = "Permission is hereby granted, free of charge"
NOTICES_MARKER = "Files: "  # COPYRIGHT.txt is a debian/copyright file: one "Files:" stanza per component.
NOTICES_HEADER = "copyright-format"


def _entries(build):
    """(name, size, read-text function) for every file in a directory or zip."""
    if zipfile.is_zipfile(build):
        z = zipfile.ZipFile(build)
        for info in z.infolist():
            if not info.is_dir():
                yield info.filename, info.file_size, (lambda i=info: z.read(i).decode("utf-8", errors="replace"))
        return
    for dirpath, _, files in os.walk(build):
        for name in files:
            path = os.path.join(dirpath, name)
            yield os.path.relpath(path, build), os.path.getsize(path), (lambda p=path: open(p, encoding="utf-8", errors="replace").read())


def check(build, max_mb=500.0, max_files=1000):
    total = count = 0
    found = {"engine_license": None, "third_party_notices": None}
    for name, size, read in _entries(build):
        total += size
        count += 1
        if os.path.splitext(name)[1].lower() not in TEXT_EXTENSIONS or size > MAX_TEXT_BYTES:
            continue
        if found["engine_license"] and found["third_party_notices"]:
            continue
        text = read()
        if not found["engine_license"] and ENGINE_COPYRIGHT in text and MIT_PERMISSION in text:
            found["engine_license"] = name
        if not found["third_party_notices"] and NOTICES_HEADER in text and text.count(NOTICES_MARKER) >= 10:
            found["third_party_notices"] = name
    problems = []
    if not found["engine_license"]:
        problems.append({"kind": "engine_license_missing", "message": "the engine's MIT license is not in the build; run: ai_loop/prepublish.py add-notices <build-dir>"})
    if not found["third_party_notices"]:
        problems.append({"kind": "third_party_notices_missing", "message": "the third-party notices (COPYRIGHT.txt) are not in the build; run: ai_loop/prepublish.py add-notices <build-dir>"})
    size_mb = total / (1024 * 1024)
    if size_mb > max_mb:
        problems.append({"kind": "too_large", "message": f"the build is {size_mb:.1f} MB, over the limit of {max_mb} MB"})
    if count > max_files:
        problems.append({"kind": "too_many_files", "message": f"the build has {count} files, over the limit of {max_files}"})
    return {"ok": not problems, "command": "check", "build": build, "size_mb": round(size_mb, 3), "files": count, "found": found, "problems": problems}


def add_notices(build):
    os.makedirs(build, exist_ok=True)
    for name in ("LICENSE.txt", "COPYRIGHT.txt"):
        shutil.copyfile(os.path.join(ROOT, name), os.path.join(build, f"ENGINE_{name}"))
    return {"ok": True, "command": "add-notices", "added": ["ENGINE_LICENSE.txt", "ENGINE_COPYRIGHT.txt"]}


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        build = os.path.join(tmp, "build")
        os.makedirs(build)
        with open(os.path.join(build, "game.pck"), "wb") as f:
            f.write(b"\0" * 300_000)
        with open(os.path.join(build, "README.txt"), "w") as f:
            f.write("My game. Made with U.ZU Engine.\n")
        r = check(build)
        assert not r["ok"] and {p["kind"] for p in r["problems"]} == {"engine_license_missing", "third_party_notices_missing"}, r
        add_notices(build)
        r = check(build)
        assert r["ok"] and r["found"] == {"engine_license": "ENGINE_LICENSE.txt", "third_party_notices": "ENGINE_COPYRIGHT.txt"}, r
        assert [p["kind"] for p in check(build, max_mb=0.1)["problems"]] == ["too_large"]
        assert [p["kind"] for p in check(build, max_files=2)["problems"]] == ["too_many_files"]
        # Only the license, not the notices: still a failure.
        os.remove(os.path.join(build, "ENGINE_COPYRIGHT.txt"))
        assert [p["kind"] for p in check(build)["problems"]] == ["third_party_notices_missing"]
        # A zip build is checked the same way.
        add_notices(build)
        archive = shutil.make_archive(os.path.join(tmp, "build"), "zip", build)
        assert check(archive)["ok"], check(archive)
    print("prepublish selftest: ok")
    return 0


def main():
    args = sys.argv[1:]
    if "--self-test" in args:
        return self_test()
    if len(args) < 2 or args[0] not in ("check", "add-notices"):
        print(__doc__, file=sys.stderr)
        return 2

    def opt(name, default):
        return type(default)(args[args.index(name) + 1]) if name in args else default

    result = check(args[1], opt("--max-mb", 500.0), opt("--max-files", 1000)) if args[0] == "check" else add_notices(args[1])
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
