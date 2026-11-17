#!/usr/bin/env python3
"""Turns structured player feedback into BACKLOG items (BACKLOG B-601).

Usage: ai_loop/feedback/to_backlog.py <feedback.json | .jsonl> [--prefix F] [--start 1] [--gifts gifts.md]
       ai_loop/feedback/to_backlog.py --self-test

Format and rules: ai_loop/feedback/FORMAT.md. Feedback is never scored, and the
player's words ("message") are never summarized or rewritten: BACKLOG items
are built from the structured fields only and refer to the words by number;
--gifts writes the words, untouched, for the author. Standard library only.
"""

import json
import os
import sys

VERSION = 1
KINDS = {"stuck": "詰まり", "bug": "不具合", "idea": "アイデア"}
SCORE_FIELDS = {"rating", "ratings", "stars", "star", "score", "scores", "points", "grade", "like", "likes"}
FIELDS = {"v", "kind", "message", "where", "build", "steps", "expected", "actual", "signature"}
HERE = os.path.dirname(os.path.abspath(__file__))


class FeedbackError(ValueError):
    pass


def load(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    stripped = text.lstrip()
    items = json.loads(text) if stripped.startswith("[") else [json.loads(l) for l in text.splitlines() if l.strip()]
    for i, item in enumerate(items, 1):
        validate(item, i)
    return items


def validate(item, number):
    if not isinstance(item, dict):
        raise FeedbackError(f"feedback #{number} is not an object")
    scored = SCORE_FIELDS & {k.lower() for k in item}
    if scored:
        raise FeedbackError(f"feedback #{number} has {sorted(scored)}: feedback is never scored (FORMAT.md)")
    unknown = set(item) - FIELDS
    if unknown:
        raise FeedbackError(f"feedback #{number} has unknown fields {sorted(unknown)}")
    if item.get("v") != VERSION:
        raise FeedbackError(f"feedback #{number}: v must be {VERSION}")
    if item.get("kind") not in KINDS:
        raise FeedbackError(f"feedback #{number}: kind must be one of {sorted(KINDS)}")
    if not isinstance(item.get("message"), str) or not item["message"].strip():
        raise FeedbackError(f"feedback #{number}: message (the player's words) is required")
    where = item.get("where", {})
    if not isinstance(where, dict) or set(where) - {"scene", "checkpoint", "cell"}:
        raise FeedbackError(f"feedback #{number}: where may only have scene, checkpoint and cell")


def _place(where):
    parts = []
    if where.get("scene"):
        parts.append(where["scene"])
    if where.get("checkpoint"):
        parts.append(f"{where['checkpoint']} 付近")
    if where.get("cell"):
        parts.append("cell " + ",".join(str(c) for c in where["cell"]))
    return " ".join(parts) if parts else "場所の指定なし"


def _judgement(kind, items):
    if kind == "stuck":
        return "自己検証プレイ（B-303）とテレメトリ（B-301）で、この場所の死亡・停滞が減ること。"
    if kind == "bug":
        steps = next((i["steps"] for i in items if i.get("steps")), None)
        if not steps:
            return "まず再現手順を確かめる（感想に手順がない）。再現できたら、その手順で再現しなくなること。"
        detail = " → ".join(steps)
        expected = next((i["expected"] for i in items if i.get("expected")), None)
        actual = next((i["actual"] for i in items if i.get("actual")), None)
        return f"手順「{detail}」で再現しなくなること" + (f"（期待: {expected}／実際: {actual}）" if expected or actual else "") + "。"
    return "作者が採るかどうかを決める。AI は作者の承認まで実装しない（設計の判断）。"


def to_backlog(items, prefix="F", start=1):
    groups = {}
    for number, item in enumerate(items, 1):
        where = item.get("where", {})
        key = (item["kind"], where.get("scene", ""), where.get("checkpoint", ""), tuple(where.get("cell", [])))
        groups.setdefault(key, []).append((number, item))
    order = {"bug": 0, "stuck": 1, "idea": 2}
    lines = []
    for n, (key, members) in enumerate(sorted(groups.items(), key=lambda kv: (order[kv[0][0]], -len(kv[1]), not any(kv[0][1:]), kv[0][1:])), start):
        kind = key[0]
        numbers = ", ".join(f"#{m}" for m, _ in members)
        builds = sorted({i["build"] for _, i in members if i.get("build")})
        build = f"（版 {', '.join(builds)}）" if builds else ""
        where = members[0][1].get("where", {})
        lines.append(f"- [ ] {prefix}-{n:03d} [{KINDS[kind]}] {_place(where)}{build}: {len(members)}件（感想 {numbers}）。判定: {_judgement(kind, [i for _, i in members])}")
    return lines


def gifts(items):
    out = ["# 届いた感想", "", "プレイヤーの言葉を、そのまま載せています（要約・言い換えなし）。", ""]
    for number, item in enumerate(items, 1):
        out.append(f"## #{number}（{KINDS[item['kind']]}）")
        out.append("")
        out.append(item["message"])
        if item.get("signature"):
            out.append("")
            out.append(f"— {item['signature']}")
        out.append("")
    return "\n".join(out)


def self_test():
    items = load(os.path.join(HERE, "sample_feedback.json"))
    lines = to_backlog(items)
    assert len(lines) == 4, lines
    assert lines[0].startswith("- [ ] F-001 [不具合] res://main.tscn cp3 付近（版 0.1）: 1件（感想 #3）"), lines[0]
    assert "右を押し続ける → ゴールまで行く" in lines[0], lines[0]
    assert lines[1].startswith("- [ ] F-002 [不具合] 場所の指定なし: 1件（感想 #5）") and "まず再現手順を確かめる" in lines[1], lines[1]
    assert lines[2].startswith("- [ ] F-003 [詰まり] res://main.tscn cp2 付近: 2件（感想 #1, #2）"), lines[2]
    assert lines[3].startswith("- [ ] F-004 [アイデア]") and "作者の承認まで実装しない" in lines[3], lines[3]
    # The player's words never go into the backlog; they reach the author untouched.
    for item in items:
        assert all(item["message"] not in line for line in lines)
        assert item["message"] in gifts(items)
    assert "— みなと" in gifts(items)
    # Scores are refused.
    for bad in ({"v": 1, "kind": "idea", "message": "x", "stars": 5}, {"v": 1, "kind": "idea", "message": "x", "Rating": 3}, {"v": 1, "kind": "praise", "message": "x"}, {"v": 1, "kind": "bug", "message": " "}):
        try:
            validate(bad, 1)
        except FeedbackError:
            continue
        raise AssertionError(f"accepted {bad}")
    print("feedback selftest: ok")
    return 0


def main():
    args = sys.argv[1:]
    if "--self-test" in args:
        return self_test()

    def opt(name, default):
        if name in args:
            i = args.index(name)
            value = args[i + 1]
            del args[i : i + 2]
            return value
        return default

    prefix, start, gifts_path = opt("--prefix", "F"), int(opt("--start", "1")), opt("--gifts", None)
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        items = load(args[0])
    except (FeedbackError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print("\n".join(to_backlog(items, prefix, start)))
    if gifts_path:
        with open(gifts_path, "w", encoding="utf-8") as f:
            f.write(gifts(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
