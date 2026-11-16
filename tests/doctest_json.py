#!/usr/bin/env python3
"""Run a tests=yes engine binary and convert doctest's XML report to JSON.

Run from the repository root:
    python3 tests/doctest_json.py bin/godot.linuxbsd.template_release.x86_64 > results.json

Additional doctest test filters may follow the executable path. Engine output
goes to stderr; stdout contains only the completed JSON report. The engine's
exit status is preserved. Missing/malformed/incomplete reports return 2 and
produce an error on stderr, never a fabricated successful test report.
"""

import json
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


def count(element: ET.Element, attribute: str) -> int:
    value = int(element.attrib[attribute])
    if value < 0:
        raise ValueError(f"negative count: {attribute}")
    return value


def convert_report(path: Path) -> dict[str, Any]:
    root = ET.parse(path).getroot()
    summary = root.find("OverallResultsTestCases")
    if root.tag != "doctest" or summary is None:
        raise ValueError("not a completed doctest XML test run")
    totals = {
        "passed": count(summary, "successes"),
        "failed": count(summary, "failures"),
        "skipped": count(summary, "skipped"),
    }
    cases = []
    for suite in root.findall("TestSuite"):
        for case in suite.findall("TestCase"):
            item: dict[str, Any] = {
                "name": case.get("name", ""),
                "suite": suite.get("name", ""),
                "file": case.attrib["filename"],
                "line": count(case, "line"),
            }
            if case.get("skipped") == "true":
                item["status"] = "skipped"
            else:
                assertions = case.find("OverallResultsAsserts")
                if assertions is None or assertions.get("test_case_success") not in ("true", "false"):
                    raise ValueError(f"missing test result: {item['name']}")
                # Raw assertion failures can be allowed by may_fail/should_fail.
                item["status"] = "passed" if assertions.get("test_case_success") == "true" else "failed"
                item["assertions"] = {
                    "passed": count(assertions, "successes"),
                    "failed": count(assertions, "failures"),
                }
            cases.append(item)
    actual = {status: sum(case["status"] == status for case in cases) for status in totals}
    if actual != totals:
        raise ValueError(f"incomplete or inconsistent test report: summary={totals}, cases={actual}")
    return {"summary": {"total": sum(totals.values()), **totals}, "test_cases": cases}


def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <engine-binary> [doctest filters...]", file=sys.stderr)
        return 2
    try:
        with tempfile.TemporaryDirectory(prefix="uzu-doctest-") as directory:
            report = Path(directory) / "results.xml"
            command = [
                str(Path(sys.argv[1]).resolve()),
                "--test",
                *sys.argv[2:],
                "--reporters=xml",
                f"--out={report}",
                "--no-skipped-summary=false",
            ]
            result = subprocess.run(command, stdout=sys.stderr)
            output = convert_report(report)
        status = result.returncode if result.returncode >= 0 else 128 - result.returncode
        output["exit_code"] = status
        json.dump(output, sys.stdout, ensure_ascii=False, indent=2)
        print()
        return status
    except (OSError, ET.ParseError, KeyError, ValueError) as error:
        print(f"doctest JSON report unavailable: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
