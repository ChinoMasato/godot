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
| B-704（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, ai_loop/iterations/ | 関数まで完了。UI の置き場所はオーナー確認待ち | 2026-11-17 |
| B-705（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, editor/scene/3d/node_3d_editor_plugin.cpp, ai_loop/iterations/ | 確認待ち（見た目はオーナー判断。補助光・街灯の UI は B-704 と一緒に確認） | 2026-11-17 |
| B-706（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, editor/scene/3d/node_3d_editor_plugin.cpp, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-711（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/palette_check.py（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-712（Q6、仮） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, ai_loop/iterations/ | 関数まで完了。UI はオーナー確認待ち | 2026-11-17 |
| U.ZU ルックの AI 向け API（オーナー指示） | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/uzu_look.{h,cpp}（新規）, scene/register_scene_types.cpp, servers/rendering/dummy/storage/texture_storage.h, doc/classes/UZULook.xml（新規）, tests/scene/test_uzu_look.cpp（新規）, ai_loop/look.sh・look_tool.gd（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-709（Q6、仮）＋エージェント入口の統合 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/look_samples/（新規）, ai_loop/look.sh, ai_loop/look_tool.gd, ai_loop/palette_check.py, scene/3d/uzu_look.{h,cpp}, tests/scene/test_uzu_look.cpp, doc/classes/UZULook.xml, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-401 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/engine.sh・engine_tool.gd・engine_selftest.sh（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-402 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/mcp_server.py（新規）, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-713 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | ai_loop/panorama_capture.gd・panorama.py（新規）, ai_loop/look.sh, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-707 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_look_preset_3d.cpp, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/iterations/ | 確認待ち | 2026-11-17 |
| B-710 | Claude Code (cloud) | claude/uzu-engine-briefing-rrno7b | editor/scene/3d/node_3d_editor_viewport.{h,cpp}, scene/3d/look_preset_3d.{h,cpp}, tests/scene/test_look_preset_3d.cpp, ai_loop/iterations/ | 確認待ち（表示はオーナーの PC で確認） | 2026-11-17 |
| B-703b | Claude Code (cloud) | ai-native（直接） | scene/resources/uzu_post_effect.{h,cpp}（新規）, scene/register_scene_types.cpp, scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.cpp, doc/classes/UZUPostEffect.xml・UZULook.xml, tests/scene/test_uzu_post_effect.cpp・test_uzu_look.cpp, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-201 | Claude Code (cloud) | ai-native（直接） | core/error/error_log_json.{h,cpp}（新規）, main/main.cpp, tests/core/test_error_log_json.cpp（新規）, ai_loop/engine.sh, ai_loop/engine_selftest.sh, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-202 | Claude Code (cloud) | ai-native（直接） | core/error/script_check_json.{h,cpp}（新規）, main/main.cpp, tests/core/test_script_check_json.cpp（新規）, ai_loop/engine.sh, ai_loop/engine_selftest.sh, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-203 | Claude Code (cloud) | ai-native（直接） | ai_loop/engine_tool.gd, ai_loop/engine.sh, ai_loop/engine_selftest.sh, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-104 | Claude Code (cloud) | ai-native（直接） | ai_loop/bench.py（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-103 | Claude Code (cloud) | ai-native（直接） | ai_loop/gdscript_errors_inventory.md（新規）, core/error/error_log_json.{h,cpp}, tests/core/test_error_log_json.cpp, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-301 | Claude Code (cloud) | ai-native（直接） | ai_loop/telemetry/（新規: FORMAT.md, telemetry.gd, report.py, sample_logs/）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-303 | Claude Code (cloud) | ai-native（直接） | ai_loop/selfplay.sh（新規）, ai_loop/selfplay_selftest.sh（新規）, ai_loop/selfplay/（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-502 | Claude Code (cloud) | ai-native（直接） | ai_loop/prepublish.py（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-601 | Claude Code (cloud) | ai-native（直接） | ai_loop/feedback/（新規: FORMAT.md, to_backlog.py, sample_feedback.json）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-302 | Claude Code (cloud) | ai-native（直接） | ai_loop/phase3_ai_player_requirements.md（新規）, ai_loop/COORDINATION.md（Q8）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-503（案） | Claude Code (cloud) | ai-native（直接） | ai_loop/bridge_api.md（新規）, ai_loop/COORDINATION.md（Q9）, ai_loop/iterations/ | 案を反映、承認待ち | 2026-11-17 |
| B-304 | Claude Code (cloud) | ai-native（直接） | ai_loop/engine_tool.gd, ai_loop/engine.sh, ai_loop/engine_selftest.sh, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-305 | Claude Code (cloud) | ai-native（直接） | ai_loop/selfplay.sh, ai_loop/selfplay/replay.gd, ai_loop/selfplay_selftest.sh, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-306 | Claude Code (cloud) | ai-native（直接） | ai_loop/aiplayer.py（新規）, ai_loop/selfplay/control.gd（新規）, ai_loop/telemetry/telemetry.gd, ai_loop/phase3_ai_player_requirements.md, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-504 | Claude Code (cloud) | ai-native（直接） | ai_loop/bridge/mock_server.py（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-505〜B-507 | Claude Code (cloud) | ai-native（直接） | ai_loop/bridge/client.py（新規）, ai_loop/publish.sh（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-801 | Claude Code (cloud) | ai-native（直接） | ai_loop/uzu.py（新規）, ai_loop/*.sh（包みに）, ai_loop/mcp_server.py, ai_loop/aiplayer.py, ai_loop/bench.py, ai_loop/bridge/client.py, ai_loop/windows/（uzu.bat 新規, README.md）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-802 | Claude Code (cloud) | ai-native（直接） | ai_loop/engine_tool.gd, ai_loop/uzu.py, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-101 | Codex | agent/b-101-codex | tests/doctest_json.py, tests/test_doctest_json.py, tests/README_JSON.md, ai_loop/iterations/2026-11-17-B-101-codex.md（運用ファイルは自分の担当宣言・状況行のみ） | 反映済み（リードが統合） | 2026-11-18 |
| B-803 | Claude Code (cloud) | ai-native（直接） | servers/rendering/shader_check_json.{h,cpp}（新規）, servers/rendering/SCsub, main/main.cpp, tests/servers/rendering/test_shader_check_json.cpp（新規）, ai_loop/uzu.py, ai_loop/bench.py, ai_loop/selfplay/frametime.gd（新規）, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-804 | Claude Code (cloud) | ai-native（直接） | ai_loop/engine_tool.gd, ai_loop/uzu.py, ai_loop/bridge/client.py, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-805 | Claude Code (cloud) | ai-native（直接） | scene/3d/lightmap_gi.{h,cpp}, doc/classes/LightmapGI.xml, tests/scene/test_lightmap_gi_bake.cpp（新規）, ai_loop/bake/（新規）, ai_loop/uzu.py, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-17 |
| B-714 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, editor/scene/3d/node_3d_editor_plugin.cpp, tests/scene/test_look_preset_3d.cpp, ai_loop/look_tool.gd, ai_loop/iterations/ | 実装は反映済み、GPU 再計測はローカルへ | 2026-11-18 |
| B-715 | Claude Code (cloud) | ai-native（直接） | scene/resources/uzu_tilt_shift.{h,cpp}（新規）, scene/register_scene_types.cpp, doc/classes/UZUTiltShift.xml（新規）, tests/scene/test_uzu_tilt_shift.cpp（新規）, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み（画像はオーナー判断） | 2026-11-18 |
| B-716 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_look_preset_3d.cpp, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み（画像はオーナー判断） | 2026-11-18 |
| B-717 | Claude Code (cloud) | ai-native（直接） | ai_loop/look_tool.gd, ai_loop/uzu.py, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-722 | Claude Code (cloud) | ai-native（直接） | scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_uzu_look.cpp, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/look_samples/hd2d/（新規）, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み（画像はオーナー判断） | 2026-11-18 |
| B-718 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_look_preset_3d.cpp, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-105 | Codex | agent/b-105-codex | tests/select_tests.py, tests/test_select_tests.py, tests/README_SELECTION.md, tests/impact_map.json, ai_loop/iterations/2026-11-17-B-105-codex.md（運用ファイルは自分の担当宣言・状況行のみ） | 反映済み（リードが統合） | 2026-11-18 |
| B-803（統合後の検証・MCP 接続） | Codex | agent/b-803-codex | ai_loop/bench.py, ai_loop/mcp_server.py, ai_loop/README_B803.md, ai_loop/iterations/2026-11-17-B-803-codex.md（運用ファイルは自分の宣言・状況行のみ） | 反映済み（リードが統合） | 2026-11-18 |
| B-005（オーナー直接依頼、ロゴ制作） | Codex | agent/b-005-codex | misc/logo/, misc/dist/html/{logo.svg,editor.html}, main/ の画像, editor/icons/ のブランド画像, editor/gui/editor_about.{cpp,h}, editor/editor_node.cpp・editor/project_manager/project_manager.cpp（About の表示名のみ）, tests/core/io/test_pck_packer.cpp（ロゴサイズ依存の検証修正）, ai_loop/iterations/2026-11-17-B-005-codex.md（運用ファイルは担当宣言・自分の行のみ） | 反映済み（リードが統合） | 2026-11-18 |
| B-719 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_uzu_look.cpp, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-720 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.{h,cpp}, scene/3d/uzu_look.{h,cpp}, doc/classes/UZULook.xml, tests/scene/test_look_preset_3d.cpp, ai_loop/look_tool.gd, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-721 | Claude Code (cloud) | ai-native（直接） | scene/resources/uzu_palette_dither.{h,cpp}（新規）, scene/register_scene_types.cpp, doc/classes/UZUPaletteDither.xml（新規）, tests/scene/test_uzu_palette_dither.cpp（新規）, ai_loop/look_tool.gd, ai_loop/mcp_server.py, ai_loop/look_samples/compare/, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-723／B-724 | Claude Code (cloud) | ai-native（直接） | scene/3d/look_preset_3d.cpp, scene/3d/uzu_look.cpp, tests/scene/test_look_preset_3d.cpp, tests/scene/test_uzu_look.cpp, ai_loop/iterations/ | 反映済み（GPU 再撮影はローカル） | 2026-11-18 |
| B-307 | Claude Code (cloud) | ai-native（直接） | ai_loop/selfplay/state_hash.gd（新規）, ai_loop/uzu.py, ai_loop/selfplay/sample_game/main.tscn, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-309 | Claude Code (cloud) | ai-native（直接） | ai_loop/selfplay/state_hash.gd, ai_loop/selfplay/control.gd, ai_loop/aiplayer.py, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-308 | Claude Code (cloud) | ai-native（直接） | core/error/error_log_json.{h,cpp}, tests/core/test_error_log_json.cpp, ai_loop/uzu.py, ai_loop/iterations/ | 反映済み | 2026-11-18 |
| B-204 | Claude Code (cloud) | ai-native（直接） | ai_loop/engine_tool.gd, ai_loop/uzu.py, ai_loop/mcp_server.py, ai_loop/iterations/ | 反映済み | 2026-11-18 |
