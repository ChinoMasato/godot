# AGENTS.md — U.ZU Engine (AI-Native Godot Fork)

This repository builds **U.ZU Engine**, a fork of Godot Engine 4.8 beta (master @ 203bf0a), developed
AI-first: AI agents are the primary developers, a human (the project owner)
owns design decisions. The full design document lives outside the repo:
`~/workspace/goals/ai-godot/files/設計書_v0.md`. Read it before changing design.

## Start here (all agents)

Read `ai_loop/BRIEFING.md` first for the project's purpose, the product
split (U.ZU Engine × Gogashare) and strategy, then `ai_loop/COORDINATION.md`
(the shared status/question board — read it at every session start and
update your own row before you finish), `ai_loop/MULTI_AGENT.md` for the
parallel-development protocol, and `ai_loop/BACKLOG.md` for the task list. Entry points per tool: Claude Code reads `CLAUDE.md` (which
imports this file); **Codex reads this `AGENTS.md` directly** (project
session defaults live in `.codex/config.toml`). The rules in this file and
in `ai_loop/` are identical for every agent, whatever the tool.

## Build (this machine)

Toolchain is a venv at `~/workspace/.venv-build` (SCons + zstandard).
System pip is PEP 668 managed; do not use it.

```sh
# Standard loop build (editor, with unit tests) — A-2, 2026-11-17.
# The editor is the standard because template builds never compile
# editor/ code and cannot use --path. Use target=template_release
# (binary: bin/godot.linuxbsd.template_release.x86_64) only for
# runtime/export checks.
~/workspace/.venv-build/bin/scons platform=linuxbsd target=editor tests=yes accesskit=no arch=x86_64 -j3

# Binary produced:
#   bin/godot.linuxbsd.editor.x86_64
```

Machine limits: ~1.5 effective CPU cores, ~6 GB RAM. A full build takes
1–1.5 h; incremental builds are the normal loop. Never start a second build
while one is running.

## Test (the oracle)

```sh
bin/godot.linuxbsd.editor.x86_64 --test        # doctest unit tests (tests/)
bin/godot.linuxbsd.editor.x86_64 --headless --quit-after 60 --path <project>  # smoke run
```

A change is "better" only if the full unit test suite passes and, where a
benchmark exists, the benchmark does not regress. No test, no loop entry.

## The loop

1. Pick exactly one item from `ai_loop/BACKLOG.md`.
2. Small diff. Write or extend a test that pins the behaviour.
3. Incremental build, run the full test suite.
4. All pass → commit on branch `ai-native`. Any fail → revert, record why.
5. Append the iteration (including failures) to `ai_loop/LEDGER.md`.

## Multi-agent development

Code development is done primarily with Claude Code (owner's decision,
2026-11-17); Codex and Muse join under the same rules. Multiple agents
develop this repo in parallel under `ai_loop/MULTI_AGENT.md`: one agent =
one backlog item = one git worktree + branch `agent/<item>-<name>`, claims
declared in `ai_loop/CLAIMS.md`, builds serialized via
`ai_loop/build_locked.sh`, per-iteration records in `ai_loop/iterations/`.
Only the lead merges into `ai-native` and appends to `ai_loop/LEDGER.md`.

## Hard rules

- One iteration = one issue. No drive-by refactors, no formatting sweeps.
- Do not modify `thirdparty/` except for a version bump approved by the owner.
- Do not break project-format compatibility (.tscn/.gd/.tres/project.godot).
- Design decisions (architecture, public API shape) need the owner's approval.
- Editor GUI policy (owner decision, 2026-11-18): the editor GUI is not a
  product surface — this engine is operated by AI agents, not by humans in a
  GUI. Do not build new editor UI features (panels, inspector menus, viewport
  widgets). Expose everything through CLI / MCP / structured output instead.
  The editor binary stays, but only as headless infrastructure (import, bake,
  export, tool-script execution). Existing UI work stays as is. Do not delete
  editor code either: upstream changes span editor and core, and deleting it
  would explode the upstream-merge surface.
- Never submit anything to the upstream Godot repository. This fork's work
  stays in this fork (upstream contribution policy prohibits AI-generated code).
- Follow upstream code style: clang-format config in repo root, GDScript
  style guide in docs. Keep the copyright/license headers intact (MIT).

## ピクセルアート描画ルール（2026-11-19 オーナー共有）
ピクセルアートのゲーム・素材を作るときは、ぼやけとガクつきを防ぐため次を守る。
- 全テクスチャは Nearest フィルタ、ミップマップなし、スプライト圧縮は Lossless。
- 基本解像度を1つ決め、拡大は整数倍のみ（余白は黒帯）。ピクセルサイズはゲーム全体で1種類に統一する。
- 移動計算は小数のまま保持し、丸めるのは描画時のみ。カメラとスプライトで丸め方をそろえる。
- 実行時にピクセルアートを回転させない（描画済みフレームか90度単位のみ）。ピクセルフォントは原寸・AAなし。
- Godot の設定: ビューポート＝基本解像度、Default Texture Filter = Nearest、Stretch は viewport＋integer、Snap 2D Transforms to Pixel = オン。
- 検証: スクリーンショットを拡大し、全ピクセルが同じ大きさの正方形で、2ピクセル幅やぼやけた縁がないことを確認する。新規素材は原寸で既存タイルと並べ、ピクセルグリッドの一致を見る。
