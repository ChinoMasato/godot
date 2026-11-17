#!/usr/bin/env python3
"""Exercise JSON conversion with the repository's actual doctest reporter.

    python3 -m unittest discover -s tests -p test_doctest_json.py -v

Requires a C++17 compiler (CXX, default c++). No engine build is performed.
The fixture lives in a temporary directory, outside engine test discovery.
"""

import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from doctest_json import convert_report

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "thirdparty/doctest/doctest.h"
#include <iostream>

TEST_CASE("pass \"quoted\" \\ slash 日本語") {
    std::cout << "engine stdout noise\n";
    std::cerr << "engine stderr noise\n";
    CHECK(1 == 1);
}
TEST_CASE("failure") { CHECK(1 == 2); }
TEST_CASE("skipped" * doctest::skip()) { CHECK(false); }
TEST_CASE("expected failure" * doctest::should_fail()) { CHECK(false); }
TEST_CASE("allowed failure" * doctest::may_fail()) { CHECK(false); }
TEST_CASE("subcases") {
    SUBCASE("passing branch") { CHECK(true); }
    SUBCASE("failing branch") { CHECK(false); }
}
"""


class ReporterIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="uzu doctest fixture ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "doctest fixture"
        source = Path(cls.temp.name) / "fixture.cpp"
        source.write_text(FIXTURE, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(
            [*compiler, "-std=c++17", "-pthread", "-I", str(ROOT), str(source), "-o", str(cls.binary)],
            check=True,
        )

    def run_report(self, *args):
        return subprocess.run(
            [sys.executable, str(ROOT / "tests/doctest_json.py"), str(self.binary), *args],
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

    def test_mixed_run_preserves_failures_and_keeps_stdout_json_only(self):
        result = self.run_report()
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["summary"], {"total": 6, "passed": 3, "failed": 2, "skipped": 1})
        self.assertEqual(report["exit_code"], 1)
        cases = {case["name"]: case for case in report["test_cases"]}
        passed = cases['pass "quoted" \\ slash 日本語']
        self.assertEqual(passed["status"], "passed")
        self.assertEqual(passed["assertions"], {"passed": 1, "failed": 0})
        self.assertGreater(passed["line"], 0)
        self.assertTrue(passed["file"].endswith("fixture.cpp"))
        self.assertEqual(cases["failure"]["status"], "failed")
        self.assertEqual(cases["subcases"]["status"], "failed")
        self.assertEqual(cases["skipped"]["status"], "skipped")
        self.assertIn("engine stdout noise", result.stderr)
        self.assertIn("engine stderr noise", result.stderr)

    def test_expected_and_allowed_assertion_failures_are_passing_cases(self):
        result = self.run_report("--test-case=*failure", "--test-case-exclude=failure")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["summary"], {"total": 6, "passed": 2, "failed": 0, "skipped": 4})
        executed = [case for case in report["test_cases"] if case["status"] != "skipped"]
        self.assertEqual(len(executed), 2)
        for case in executed:
            self.assertEqual(case["status"], "passed")
            self.assertEqual(case["assertions"]["failed"], 1)

    def test_passing_filter(self):
        result = self.run_report("--test-case=pass*")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["summary"], {"total": 6, "passed": 1, "failed": 0, "skipped": 5})

    def test_empty_filter_has_no_fabricated_passes(self):
        result = self.run_report("--test-case=nonexistent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["summary"], {"total": 6, "passed": 0, "failed": 0, "skipped": 6})

    def test_query_is_not_a_completed_run(self):
        result = self.run_report("--list-test-cases")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("report unavailable", result.stderr)

    def test_cannot_suppress_skipped_summary(self):
        result = self.run_report("--test-case=pass*", "--no-skipped-summary=true")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["summary"]["skipped"], 5)


class InvalidReportTests(unittest.TestCase):
    def convert(self, xml):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.xml"
            path.write_text(xml, encoding="utf-8")
            return convert_report(path)

    def test_truncated_xml(self):
        with self.assertRaises(ET.ParseError):
            self.convert("<doctest><TestSuite>")

    def test_summary_cannot_claim_passes_without_cases(self):
        with self.assertRaises(ValueError):
            self.convert('<doctest><OverallResultsTestCases successes="1" failures="0" skipped="0"/></doctest>')

    def test_wrong_reporter(self):
        with self.assertRaises(ValueError):
            self.convert("<testsuites/>")

    def test_missing_binary(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tests/doctest_json.py"), "/nonexistent/uzu-engine"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
