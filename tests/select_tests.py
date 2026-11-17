#!/usr/bin/env python3
"""Plan or run doctest source-file filters from a B-101 JSON catalog and impact map.

Unknown changes select the full suite. This is a development shortcut; the final
integration check must still run the full suite. See README_SELECTION.md.
"""

import argparse
import fnmatch
import json
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent


def repo_path(value: str) -> str:
    """Accept repository-relative paths, including Windows separators."""
    value = value.replace("\\", "/")
    while value.startswith("./"):
        value = value[2:]
    if value in ("", ".") or value.startswith("/") or ":" in value or ".." in value.split("/"):
        raise ValueError(f"not a repository-relative path: {value!r}")
    return str(PurePosixPath(value))


def test_path(value: str) -> str:
    """Reports may use an absolute compile-time path from another worktree."""
    value = value.replace("\\", "/")
    while value.startswith("./"):
        value = value[2:]
    if value.startswith("/") or ":" in value:
        for marker in ("/modules/", "/tests/"):
            if marker in value:
                value = marker[1:] + value.split(marker, 1)[1]
                break
    value = repo_path(value)
    is_test = value.startswith("tests/") or (value.startswith("modules/") and "/tests/" in value)
    if not is_test or any(c in value for c in ",*?[]"):
        raise ValueError(f"unsupported doctest source path: {value!r}")
    return value


def select(catalog: dict, mapping: dict, changed: list[str]) -> dict:
    if not isinstance(catalog, dict) or not isinstance(mapping, dict):
        raise ValueError("catalog and impact map must be JSON objects")
    if mapping.get("version") != 1 or not isinstance(mapping.get("rules"), list):
        raise ValueError("impact map needs version 1 and a rules array")
    cases = catalog.get("test_cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("catalog needs a nonempty B-101 test_cases array")
    if any(
        not isinstance(case, dict) or not isinstance(case.get("file"), str) or not isinstance(case.get("name"), str)
        for case in cases
    ):
        raise ValueError("catalog cases need string file and name fields")
    actual = {status: sum(case.get("status") == status for case in cases) for status in ("passed", "failed", "skipped")}
    summary = catalog.get("summary", {})
    if sum(actual.values()) != len(cases) or summary != {"total": len(cases), **actual}:
        raise ValueError("catalog summary is incomplete or inconsistent with its cases")
    files = {test_path(case["file"]) for case in cases}
    rules = []
    for rule in mapping["rules"]:
        if not isinstance(rule, dict) or not isinstance(rule.get("source"), str):
            raise ValueError("map rules need a source string")
        source = repo_path(rule["source"])
        targets = rule["tests"]
        if not isinstance(targets, list) or not targets or any(not isinstance(target, str) for target in targets):
            raise ValueError(f"map rule has no tests: {source}")
        rules.append((source, {test_path(target) for target in targets}))
    changed = sorted({repo_path(path) for path in changed})
    selected, reasons, unresolved = set(), [], []
    if not changed:
        unresolved.append("no changed paths supplied")
    for path in changed:
        targets = set()
        if path in files:
            targets.add(path)
        for source, tests in rules:
            if fnmatch.fnmatchcase(path, source):
                targets.update(tests)
        missing = targets - files
        if not targets or missing:
            unresolved.append(
                f"{path}: "
                + ("unmapped" if not targets else "tests absent from catalog: " + ", ".join(sorted(missing)))
            )
        else:
            selected.update(targets)
            reasons.append({"path": path, "test_files": sorted(targets)})
    full = bool(unresolved)
    selected = files if full else selected
    names = [case["name"] for case in cases if test_path(case["file"]) in selected and case.get("status") != "skipped"]
    if not names:
        # A catalog from a previously filtered run cannot prove which tests exist.
        full = True
        selected = files
        unresolved.append("selection has no runnable cases in the catalog")
        names = [case["name"] for case in cases if case.get("status") != "skipped"]
    return {
        "mode": "all" if full else "selected",
        "changed": changed,
        "test_files": sorted(selected),
        "catalog_cases": len(names),
        "reasons": reasons,
        "fallback_reasons": unresolved,
        "doctest_args": [] if full else ["--source-file=" + ",".join("*" + name for name in sorted(selected))],
        "requires_full_suite_before_merge": True,
    }


def git_changes(root: Path, base: str) -> list[str]:
    """Include committed, staged, unstaged and untracked paths; renames include both."""
    commands = [
        ["git", "diff", "--no-renames", "--name-only", "-z", base, "--"],
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
    ]
    if not base or base.startswith("-"):
        raise ValueError("git base must be a revision, not an option")
    paths = set()
    for command in commands:
        output = subprocess.run(command, cwd=root, check=True, capture_output=True).stdout
        paths.update(path.decode("utf-8") for path in output.split(b"\0") if path)
    return sorted(paths)


def run_selection(engine: Path, plan: dict) -> tuple[dict, int]:
    with tempfile.TemporaryDirectory(prefix="uzu-selection-") as directory:
        result = Path(directory) / "result.json"
        with result.open("w", encoding="utf-8") as output:
            process = subprocess.run(
                [sys.executable, str(HERE / "doctest_json.py"), str(engine.resolve()), *plan["doctest_args"]],
                stdout=output,
            )
        if not result.stat().st_size:
            raise ValueError("test runner produced no completed JSON report")
        report = json.loads(result.read_text(encoding="utf-8"))
    if not report["summary"]["passed"] and not report["summary"]["failed"]:
        raise ValueError("no tests executed; refresh the catalog and run the full suite")
    if plan["mode"] == "selected":
        executed = {test_path(case["file"]) for case in report["test_cases"] if case["status"] != "skipped"}
        if executed != set(plan["test_files"]):
            raise ValueError("executed source files differ from the selection; refresh the catalog")
    return {"selection": plan, **report}, process.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True, help="Full B-101 JSON test report")
    parser.add_argument("--map", type=Path, default=HERE / "impact_map.json")
    parser.add_argument("--changed", nargs="*", help="Explicit repository-relative paths")
    parser.add_argument("--base", help="Git base for committed and working-tree changes")
    parser.add_argument("--root", type=Path, default=HERE.parent)
    parser.add_argument("--engine", type=Path, help="Run the selected tests; omit to print only the plan")
    options = parser.parse_args()
    try:
        if options.changed is not None and options.base is not None:
            raise ValueError("use either --changed or --base")
        changed = options.changed if options.changed is not None else git_changes(options.root, options.base or "HEAD")
        plan = select(
            json.loads(options.catalog.read_text(encoding="utf-8")),
            json.loads(options.map.read_text(encoding="utf-8")),
            changed,
        )
        output, status = run_selection(options.engine, plan) if options.engine else ({"selection": plan}, 0)
        json.dump(output, sys.stdout, ensure_ascii=False, indent=2)
        print()
        return status
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
