# AGENTS.md — U.ZU Engine (AI-Native Godot Fork)

This repository builds **U.ZU Engine**, a fork of Godot Engine 4.8 beta (master @ 203bf0a), developed
AI-first: AI agents are the primary developers, a human (the project owner)
owns design decisions. The full design document lives outside the repo:
`~/workspace/goals/ai-godot/files/設計書_v0.md`. Read it before changing design.

## Build (this machine)

Toolchain is a venv at `~/workspace/.venv-build` (SCons + zstandard).
System pip is PEP 668 managed; do not use it.

```sh
# Baseline / test build (template, with unit tests)
~/workspace/.venv-build/bin/scons platform=linuxbsd target=template_release tests=yes accesskit=no arch=x86_64 -j3

# Binary produced:
#   bin/godot.linuxbsd.template_release.x86_64
```

Machine limits: ~1.5 effective CPU cores, ~6 GB RAM. A full build takes
1–1.5 h; incremental builds are the normal loop. Never start a second build
while one is running.

## Test (the oracle)

```sh
bin/godot.linuxbsd.template_release.x86_64 --test        # doctest unit tests (tests/)
bin/godot.linuxbsd.template_release.x86_64 --headless --quit-after 60 --path <project>  # smoke run
```

A change is "better" only if the full unit test suite passes and, where a
benchmark exists, the benchmark does not regress. No test, no loop entry.

## The loop

1. Pick exactly one item from `ai_loop/BACKLOG.md`.
2. Small diff. Write or extend a test that pins the behaviour.
3. Incremental build, run the full test suite.
4. All pass → commit on branch `ai-native`. Any fail → revert, record why.
5. Append the iteration (including failures) to `ai_loop/LEDGER.md`.

## Hard rules

- One iteration = one issue. No drive-by refactors, no formatting sweeps.
- Do not modify `thirdparty/` except for a version bump approved by the owner.
- Do not break project-format compatibility (.tscn/.gd/.tres/project.godot).
- Design decisions (architecture, public API shape) need the owner's approval.
- Never submit anything to the upstream Godot repository. This fork's work
  stays in this fork (upstream contribution policy prohibits AI-generated code).
- Follow upstream code style: clang-format config in repo root, GDScript
  style guide in docs. Keep the copyright/license headers intact (MIT).
