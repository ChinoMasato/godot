# Test impact selection (B-105)

Use a full B-101 JSON report and an explicit source-to-test map to run a smaller
set during development. The final integration check still requires the complete
suite (`ai_loop/agent_finish.sh`). The map is a maintained hint, not a dependency
analysis or a guarantee that every affected test has been found.

Run from the repository root with Python 3.10 or later:

```sh
# Refresh the catalog after rebuilding or changing test registration/configuration.
python3 tests/doctest_json.py bin/godot.linuxbsd.editor.x86_64 > /tmp/uzu-catalog.json

# Inspect a plan without launching the engine.
python3 tests/select_tests.py --catalog /tmp/uzu-catalog.json \
  --changed core/error/error_log_json.cpp

# Run the selected source files and return the B-101 report plus selection details.
python3 tests/select_tests.py --catalog /tmp/uzu-catalog.json \
  --changed core/error/error_log_json.cpp \
  --engine bin/godot.linuxbsd.editor.x86_64 > /tmp/uzu-selected.json

# Derive changed paths from Git instead: commits since the base, index, working
# tree and untracked files. A rename includes both its old and new paths.
python3 tests/select_tests.py --catalog /tmp/uzu-catalog.json \
  --base origin/ai-native --engine bin/godot.linuxbsd.editor.x86_64
```

`--changed` and `--base` are mutually exclusive. Without either, use changes
against HEAD, including untracked files. Generated or ignored files are excluded
by Git. Paths supplied explicitly are relative to the repository root; Windows
separators are supported. `--root` changes the Git working directory.

`tests/impact_map.json` is version 1. Each rule contains a repository-relative
`source` glob and a nonempty `tests` array of doctest source paths. All matching
rules are combined. A changed test source listed in the catalog selects itself,
including `modules/<name>/tests/...`. Multiple cases in one source are run together
using doctest's existing `--source-file` option. Test names are never used as
filters, avoiding their comma/wildcard syntax.

The initial map covers the U.ZU JSON diagnostics, shader checks, look presets and
post effect tests. Extend it when evidence establishes more useful relationships.
Changes to build files, global infrastructure or other sources without a map run
the full suite. A mapped test missing from the catalog, an empty change list, or
a selection containing only skipped cases also falls back to the full suite.
No special exemption silently ignores documentation or tool changes.

A plan contains `mode` (`selected` or `all`), `changed`, `test_files`,
`catalog_cases`, `reasons`, `fallback_reasons`, `doctest_args`, and
`requires_full_suite_before_merge: true`. `catalog_cases` counts runnable cases in
the input catalog for that plan. It is not a count of assertions or a prediction
of newly added tests. When running, the top-level `summary`, `test_cases` and
`exit_code` are the actual B-101 results. Cases excluded by the filter appear as
skipped, so the report's total includes them; use passed+failed for executed cases.

Exit status is the engine's status for a completed report, or 2 for invalid input,
missing/incomplete output, zero executed tests, or a different executed source
set than selected. Do not reuse a filtered report as a full catalog. Rebuild and
refresh the full catalog whenever tests or build options change. The tool cannot
detect every stale catalog or infer transitive dependencies from this small map.

## Verification

```sh
python3 -m unittest discover -s tests -p test_select_tests.py -v
python3 -m unittest discover -s tests -p test_doctest_json.py -v
```

The integration test compiles a small, isolated C++ doctest fixture using this
repository's existing header (without modifying it). It verifies a passing
baseline, then introduces a behavior regression and checks that filtered and
full runs report the same failing case and nonzero status. If no C++ compiler is
available, that fixture is explicitly skipped. Other tests cover map unions,
unknown changes, missing targets, path normalization, invalid catalogs, and Git
rename/staged/unstaged/untracked paths. This fixture does not modify engine code.
