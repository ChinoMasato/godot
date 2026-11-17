#!/usr/bin/env python3
"""B-105 selection rules and real doctest pass/fail equivalence."""

import copy
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import select_tests

HERE = Path(__file__).resolve().parent


def catalog():
    return {
        "summary": {"total": 2, "passed": 2, "failed": 0, "skipped": 0},
        "test_cases": [
            {"name": "related", "file": "tests/sample/test_related.h", "status": "passed"},
            {"name": "other", "file": "tests/sample/test_other.h", "status": "passed"},
        ],
    }


def mapping():
    return {"version": 1, "rules": [{"source": "core/sample.*", "tests": ["tests/sample/test_related.h"]}]}


class SelectionTests(unittest.TestCase):
    def test_known_change_selects_only_related_source(self):
        plan = select_tests.select(catalog(), mapping(), ["core/sample.cpp", "core/sample.cpp"])
        self.assertEqual(plan["mode"], "selected")
        self.assertEqual(plan["catalog_cases"], 1)
        self.assertEqual(plan["doctest_args"], ["--source-file=*tests/sample/test_related.h"])
        self.assertTrue(plan["requires_full_suite_before_merge"])

    def test_all_matching_rules_are_combined(self):
        rules = mapping()
        rules["rules"].append({"source": "core/*.cpp", "tests": ["tests/sample/test_other.h"]})
        plan = select_tests.select(catalog(), rules, ["core/sample.cpp"])
        self.assertEqual(plan["catalog_cases"], 2)

    def test_changed_test_file_selects_itself(self):
        plan = select_tests.select(catalog(), mapping(), ["tests/sample/test_other.h"])
        self.assertEqual(plan["test_files"], ["tests/sample/test_other.h"])

    def test_unknown_change_expands_entire_plan(self):
        plan = select_tests.select(catalog(), mapping(), ["core/sample.cpp", "SConstruct"])
        self.assertEqual(plan["mode"], "all")
        self.assertEqual(plan["doctest_args"], [])
        self.assertIn("SConstruct: unmapped", plan["fallback_reasons"])

    def test_missing_target_cannot_silently_drop_tests(self):
        rules = mapping()
        rules["rules"][0]["tests"].append("tests/missing.h")
        plan = select_tests.select(catalog(), rules, ["core/sample.cpp"])
        self.assertEqual(plan["mode"], "all")
        self.assertIn("absent", plan["fallback_reasons"][0])

    def test_no_changes_falls_back_to_full_suite(self):
        self.assertEqual(select_tests.select(catalog(), mapping(), [])["mode"], "all")

    def test_invalid_catalog_rejected(self):
        data = catalog()
        data["summary"]["total"] = 1
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            select_tests.select(data, mapping(), ["core/sample.cpp"])
        data = catalog()
        data["test_cases"][0]["status"] = "unknown"
        with self.assertRaises(ValueError):
            select_tests.select(data, mapping(), ["core/sample.cpp"])

    def test_nonobject_json_and_malformed_records_rejected(self):
        for data in (None, [], {"test_cases": [None]}, {"test_cases": [{"name": "x", "file": 4}]}):
            with self.subTest(data=data), self.assertRaises(ValueError):
                select_tests.select(data, mapping(), ["core/sample.cpp"])
        for rules in (
            None,
            [],
            {"version": 1, "rules": [None]},
            {"version": 1, "rules": [{"source": "core/*", "tests": [None]}]},
        ):
            with self.subTest(rules=rules), self.assertRaises(ValueError):
                select_tests.select(catalog(), rules, ["core/sample.cpp"])

    def test_skipped_only_selection_falls_back(self):
        data = catalog()
        data["test_cases"][0]["status"] = "skipped"
        data["summary"].update(passed=1, skipped=1)
        plan = select_tests.select(data, mapping(), ["core/sample.cpp"])
        self.assertEqual(plan["mode"], "all")
        self.assertEqual(plan["catalog_cases"], 1)

    def test_absolute_catalog_paths_and_windows_paths(self):
        data = catalog()
        data["test_cases"][0]["file"] = r"C:\build\tests\sample\test_related.h"
        plan = select_tests.select(data, mapping(), [r".\core\sample.cpp"])
        self.assertEqual(plan["doctest_args"], ["--source-file=*tests/sample/test_related.h"])

    def test_module_test_paths_keep_their_module_prefix(self):
        for path in ("./modules/csg/tests/test_csg.h", "/build/engine/modules/csg/tests/test_csg.h"):
            self.assertEqual(select_tests.test_path(path), "modules/csg/tests/test_csg.h")
        data = catalog()
        data["test_cases"][0]["file"] = "./modules/csg/tests/test_csg.h"
        plan = select_tests.select(data, mapping(), ["modules/csg/tests/test_csg.h"])
        self.assertEqual(plan["doctest_args"], ["--source-file=*modules/csg/tests/test_csg.h"])

    def test_invalid_paths_and_filter_metacharacters_rejected(self):
        for path in ("../core/a.cpp", "/core/a.cpp", "C:/a.cpp", ".", ""):
            with self.subTest(path=path), self.assertRaises(ValueError):
                select_tests.repo_path(path)
        data = catalog()
        data["test_cases"][0]["file"] = "tests/a,b.h"
        with self.assertRaises(ValueError):
            select_tests.select(data, mapping(), ["core/sample.cpp"])

    def test_empty_or_unknown_map_rejected(self):
        for data in ({"version": 2, "rules": []}, {"version": 1, "rules": [{"source": "core/*", "tests": []}]}):
            with self.assertRaises(ValueError):
                select_tests.select(catalog(), data, ["core/sample.cpp"])

    def test_git_changes_include_rename_dirty_staged_and_untracked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*args):
                return subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)

            git("init", "-q")
            git("config", "user.name", "Selection test")
            git("config", "user.email", "selection@example.invalid")
            for name in ("old.cpp", "dirty.cpp", "staged.cpp"):
                (root / name).write_text("before\n", encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "baseline")
            git("mv", "old.cpp", "new.cpp")
            git("commit", "-qm", "rename")
            (root / "dirty.cpp").write_text("after\n", encoding="utf-8")
            (root / "staged.cpp").write_text("after\n", encoding="utf-8")
            git("add", "staged.cpp")
            (root / "untracked name.cpp").write_text("new\n", encoding="utf-8")
            self.assertEqual(
                select_tests.git_changes(root, "HEAD~1"),
                ["dirty.cpp", "new.cpp", "old.cpp", "staged.cpp", "untracked name.cpp"],
            )
            with self.assertRaises(ValueError):
                select_tests.git_changes(root, "--help")


@unittest.skipUnless(shutil.which("c++"), "integration fixture needs a C++ compiler")
class RealDoctestTests(unittest.TestCase):
    def test_selected_and_full_runs_detect_the_same_sample_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "sample.cpp"
            header = (HERE.parent / "thirdparty/doctest/doctest.h").as_posix()
            source.write_text(
                "#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n"
                "#define DOCTEST_CONFIG_NO_POSIX_SIGNALS\n"
                f'#include "{header}"\n'
                '#line 1 "tests/sample/test_related.h"\n'
                'TEST_CASE("related") { CHECK(2 + 2 == EXPECTED); }\n'
                '#line 1 "tests/sample/test_other.h"\n'
                'TEST_CASE("other") { CHECK(1 == 1); }\n',
                encoding="utf-8",
            )
            binary = root / "sample"

            def compile_sample(expected):
                subprocess.run(
                    ["c++", "-std=c++17", f"-DEXPECTED={expected}", str(source), "-o", str(binary)],
                    check=True,
                    capture_output=True,
                )

            def run_all():
                proc = subprocess.run(
                    [sys.executable, str(HERE / "doctest_json.py"), str(binary)], capture_output=True, text=True
                )
                return json.loads(proc.stdout), proc.returncode

            compile_sample(4)
            before, status = run_all()
            self.assertEqual(status, 0)
            plan = select_tests.select(before, mapping(), ["core/sample.cpp"])
            selected, status = select_tests.run_selection(binary, plan)
            self.assertEqual(status, 0)
            self.assertEqual(selected["summary"]["passed"], 1)
            compile_sample(5)  # Introduce a real C++ behavior regression.
            full, full_status = run_all()
            selected, selected_status = select_tests.run_selection(binary, plan)

            def failures(report):
                return {case["name"] for case in report["test_cases"] if case["status"] == "failed"}

            self.assertEqual(full_status, 1)
            self.assertEqual(selected_status, full_status)
            self.assertEqual(failures(selected), failures(full))
            self.assertEqual(failures(full), {"related"})
            self.assertEqual(selected["summary"]["failed"], 1)
            self.assertEqual(selected["summary"]["passed"], 0)
            wrong = copy.deepcopy(plan)
            wrong["doctest_args"] = ["--source-file=*tests/nonexistent.h"]
            with self.assertRaisesRegex(ValueError, "no tests executed"):
                select_tests.run_selection(binary, wrong)


if __name__ == "__main__":
    unittest.main()
