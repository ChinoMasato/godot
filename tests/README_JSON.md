# Doctest results as JSON (B-101)

Run from the repository root using an existing `tests=yes` engine build:

```sh
python3 tests/doctest_json.py bin/godot.linuxbsd.editor.x86_64 > results.json
```

The command runs the engine with its existing doctest XML reporter, reads the
temporary XML file, and writes JSON to stdout. Engine stdout/stderr go to stderr.
No engine API, reporter, or third-party code changes are required. Python 3.10+
and the standard library suffice.

Doctest test filters can follow the executable:

```sh
python3 tests/doctest_json.py bin/godot.linuxbsd.editor.x86_64 --test-case='[String]*' > string-results.json
```

Use this command for test execution, not doctest query commands such as
`--list-test-cases`. Do not supply reporter or output options; the script owns
the XML report destination. Temporary files are removed after conversion.

The output contains:

- `summary`: `total`, `passed`, `failed`, `skipped` counts.
- `test_cases`: each case's `name`, `suite`, `file`, `line`, and `status`.
  Executed cases also include `assertions.passed` and `assertions.failed`.
- `exit_code`: the engine process exit code (signals become `128 + signal`).

`status` is `passed`, `failed`, or `skipped`. The converter uses doctest's
`test_case_success`, so expected/allowed assertion failures remain passing
test cases. `skipped` includes cases excluded by filters. A run with no matching
tests can exit zero with zero passes; check counts when validating a full run.
Summary counts must match the individual case results.

The command preserves the engine exit code, including nonzero test failure
codes. JSON is still written for a completed failing run. Read that JSON even
when the process fails; do not put the parsing step behind a shell `&&`.
Environment failures are reported as failures, without exemptions.

Missing executables, missing/truncated XML, query output, and inconsistent
results produce exit code 2 with a diagnostic on stderr and no JSON report.

Verify the converter against this repository's actual doctest header:

```sh
python3 -m unittest discover -s tests -p test_doctest_json.py -v
```

These tests compile a tiny temporary C++17 fixture (`CXX`, default `c++`) and
exercise real passing/failing/skipped cases and subcases. They do not build or
run the engine's complete test suite.
