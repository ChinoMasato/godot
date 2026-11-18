"""Compare replay JSON logs or verify selected snapshots against a log."""

import argparse
import json
import math
from pathlib import Path


def invalid_number(value):
    raise ValueError(f"non-JSON number: {value}")


def finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        invalid_number(value)
    return number


def read_log(path):
    with path.open(encoding="utf-8-sig") as file:
        if path.suffix.lower() == ".jsonl":
            records = []
            for index, line in enumerate(file, 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line, parse_constant=invalid_number, parse_float=finite_float)
                except ValueError as error:
                    raise ValueError(f"{path}:{index}: {error}") from error
                if not isinstance(record, dict):
                    raise ValueError(f"{path}:{index}: expected a JSON object")
                records.append(record)
            result = records
        else:
            result = json.load(file, parse_constant=invalid_number, parse_float=finite_float)
    if not isinstance(result, (dict, list)) or not result:
        raise ValueError(f"{path}: expected a non-empty JSON object or list")
    return result


def pointer(path):
    return "".join("/" + str(part).replace("~", "~0").replace("/", "~1") for part in path)


def first_diff(expected, actual, path=(), subset=False):
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(expected):
            field = path + (key,)
            if key not in actual:
                return {"path": pointer(field), "reason": "missing", "expected": expected[key]}
            diff = first_diff(expected[key], actual[key], field, subset)
            if diff is not None:
                return diff
        if not subset:
            extra = sorted(actual.keys() - expected.keys())
            if extra:
                return {"path": pointer(path + (extra[0],)), "reason": "unexpected", "actual": actual[extra[0]]}
        return None
    if isinstance(expected, list) and isinstance(actual, list):
        for index, (left, right) in enumerate(zip(expected, actual)):
            diff = first_diff(left, right, path + (index,), subset)
            if diff is not None:
                return diff
        if len(expected) != len(actual):
            index = min(len(expected), len(actual))
            field = pointer(path + (index,))
            if len(expected) > len(actual):
                return {"path": field, "reason": "missing", "expected": expected[index]}
            return {"path": field, "reason": "unexpected", "actual": actual[index]}
        return None
    if expected != actual or isinstance(expected, bool) != isinstance(actual, bool):
        return {"path": pointer(path), "reason": "value", "expected": expected, "actual": actual}
    return None


def position(record):
    if isinstance(record, dict):
        return {key: record[key] for key in ("frame", "time") if key in record}
    return {}


def compare_exact(expected, actual):
    if isinstance(expected, list) and isinstance(actual, list):
        for index in range(max(len(expected), len(actual))):
            left = expected[index] if index < len(expected) else None
            right = actual[index] if index < len(actual) else None
            if index >= len(actual):
                diff = {"path": "", "reason": "missing", "expected": left}
            elif index >= len(expected):
                diff = {"path": "", "reason": "unexpected", "actual": right}
            else:
                diff = first_diff(left, right)
            if diff is not None:
                return {**(position(left) or position(right)), "record": index + 1, **diff}
        return None
    diff = first_diff(expected, actual)
    return {**position(expected), **diff} if diff is not None else None


def compare_snapshots(expected, actual):
    if not isinstance(actual, list):
        return first_diff(expected, actual, subset=True)
    snapshots = expected if isinstance(expected, list) else [expected]
    # Keep the timeline's order and consume every matched record only once.
    cursor = 0
    for index, snapshot in enumerate(snapshots, 1):
        at = position(snapshot)
        if not at:
            raise ValueError(f"snapshot {index} needs a frame or time")
        selectors = {**at, **{key: snapshot[key] for key in ("type", "kind") if key in snapshot}}
        mismatch = None
        for offset in range(cursor, len(actual)):
            record = actual[offset]
            if not isinstance(record, dict) or first_diff(selectors, record, subset=True) is not None:
                continue
            diff = first_diff(snapshot, record, subset=True)
            if diff is None:
                cursor = offset + 1
                break
            if mismatch is None:
                mismatch = {**at, "record": offset + 1, **diff}
        else:
            return mismatch or {**at, "snapshot": index, "path": "", "reason": "missing_snapshot"}
    return None


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument(
        "--snapshots", action="store_true", help="Match only the expected fields at each listed frame or time"
    )
    args = parser.parse_args(argv)
    try:
        expected = read_log(args.expected)
        actual = read_log(args.actual)
        mode = "snapshots" if args.snapshots else "exact"
        diff = compare_snapshots(expected, actual) if args.snapshots else compare_exact(expected, actual)
        report = {"ok": diff is None, "mode": mode, "first_mismatch": diff}
        print(json.dumps(report, ensure_ascii=False))
        return 0 if diff is None else 1
    except (OSError, ValueError, RecursionError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        return 2
