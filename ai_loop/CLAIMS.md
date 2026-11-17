# CLAIMS — 担当宣言ボード

複数エージェントの同時開発で、誰がどこを触っているかを示すボード。管理は統合担当（リード）。
エージェントは着手前に1行追加し、終わったら状態を更新する（手順は `MULTI_AGENT.md`）。

状態: 作業中 / 確認待ち（テスト pass・マージ待ち） / マージ済 / 破棄

| 項目 | 担当 | ブランチ | 触るパス | 状態 | 開始日 |
|---|---|---|---|---|---|
| B-004 | Muse (VM) | ai-native（基盤整備のため例外的に直接） | CLAUDE.md, ai_loop/ | マージ済 | 2026-11-17 |
| B-001 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/LEDGER.md, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-002 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/measure.sh（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-003 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | version.py（name のみ）, tests/core/test_version.cpp（新規）, ai_loop/iterations/ | 確認待ち（案A で実装） | 2026-11-17 |
| B-701（仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}（新規）, tests/scene/test_look_preset_3d.cpp（新規）, editor/scene/3d/node_3d_editor_plugin.{h,cpp}, ai_loop/iterations/ | 確認待ち（ボタンの手動確認はオーナーの PC で） | 2026-11-17 |
| A-1（Q5、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/screenshot.sh（新規）, ai_loop/image_diff.py（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| A-3（Q5、仮、B-102 相当） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | core/io/ip.cpp, tests/core/io/test_ip.cpp, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-702（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, editor/scene/3d/node_3d_editor_plugin.{h,cpp}, ai_loop/iterations/ | 確認待ち（見た目はオーナー判断） | 2026-11-17 |
| B-703（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, ai_loop/iterations/ | 確認待ち（見た目はオーナー判断） | 2026-11-17 |
