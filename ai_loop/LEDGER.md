# LEDGER — 検証ループの実行記録

失敗も消さない。1イテレーション = 1行ブロック。

## 2026-11-17 プロジェクト開始

- フォーク点: Godot master @ 203bf0a（4.8 beta）。ブランチ `ai-native` を作成。
- 環境実測: clone 約2分／477MB。SCons は venv（~/workspace/.venv-build）に導入。
- ビルドプローブ: template_release で4分間に178オブジェクト（約44個/分）。全対象は約2,000ファイル、フルビルド推定1〜1.5時間。
- ベースラインビルド（template_release, tests=yes）をバックグラウンドで開始。結果は完了時にここへ追記する。

## 2026-11-17 — B-004 マルチエージェント開発基盤（Muse、VM）
- 千野の決定: コード開発の主ツールを Claude Code とし、複数 AI エージェントで並行開発できる構成にする。
- 追加: CLAUDE.md（AGENTS.md を @import）、ai_loop/MULTI_AGENT.md、ai_loop/CLAIMS.md、ai_loop/iterations/、agent_start.sh／build_locked.sh（flock 直列化）／agent_finish.sh。
- 実証: agent_start.sh で B-999 テスト用 worktree＋ブランチの作成・削除を確認。スクリプトは bash -n pass。
- この VM にも Claude Code 2.1.325 を導入（~/workspace/tools/claude-code）。初回利用時の Anthropic サインインは千野側の操作が必要。
- ベースラインビルド（B-001）は継続中につき、本件はコード変更なし・運用ファイルのみ。

## 2026-11-17 — GitHub へ展開（A案: 正式フォーク）
- 千野が GitHub で godotengine/godot をフォーク（ChinoMasato/godot、master はフォーク点 203bf0a のまま）。
- Muse の GitHub App にはフォーク作成権限がなかったため、フォーク作成は千野が Web で実施。push はデバイス認証で発行した GitHub CLI トークンで Muse 環境から実行し、push 完了後にトークンファイルは破棄した。
- ai-native を push し、リモート HEAD がローカル HEAD（29765fe）と一致することを ls-remote で確認済み。以後の正は GitHub 側リポジトリとする。

## 2026-11-17 — Codex 対応（千野の指示）
- Codex でもそのまま作業できるよう入口を明示: AGENTS.md 冒頭に「Start here」節を追加（BRIEFING → MULTI_AGENT → BACKLOG の必読順と、ツール別入口: Claude Code=CLAUDE.md／Codex=AGENTS.md）。
- .codex/config.toml を追加（プロジェクト既定値: sandbox_mode=workspace-write、approval_policy=on-request。規則ではなく既定値）。
- BRIEFING.md にツール別入口の注記を追加。規則自体は従来どおり全ツール共通。

## 2026-11-17 — 三者連携基盤（千野の指示）
- 連携窓口を ai_loop/COORDINATION.md に一本化（状況ボード＋質問節＋役割定義）。開始時に読み、終了時に自分の行を更新する運用。正式決定はオーナー承認→リードがLEDGERへ反映した時点で確定、と明記。
- MULTI_AGENT.md に連携節を追加、AGENTS.md と BRIEFING.md の必読リストに COORDINATION.md を追加。
- 背景: Claude Code セッションと Codex が稼働開始し、質問（B-001採用可否・作業ブランチ・分担案）と push 403 の報告がオーナー経由で届いたため、中継を不要にする仕組みとして整備。未決事項は COORDINATION.md の Q1/Q2 に集約。

## 2026-11-17 — Q1 決定（オーナー承認）と B-001 ベースライン採用
- 決定1（B-001）: Claude Code（ローカル）が取得したベースラインを正式採用する。報告値: フルビルド 15分54秒、単体テスト 1431件中 1430 pass、fail 1件は環境起因（Claude Code の報告）。エンジン側コミット ff5246b は運用ファイルのみでコード差分がないため、この結果は現行ツリーに適用できる。詳細記録（ビルド設定・マシン環境・fail 1件のテスト名と根拠）は Claude Code が追って記録し、リードが ai-native へ取り込む。Muse の VM ビルドは本決定により停止した（オブジェクト 2,111 個は保持、増分再開可能。VM 側のテスト結果は得られていない）。
- 決定2（作業ブランチ）: Claude セッション専用ブランチ claude/uzu-engine-briefing-rrno7b を許可。ai-native へのマージはリード経由。
- 決定3（分担）: Claude Code = B-002 → B-003、Codex = B-101 を承認。B-002/B-003 に限り ai_loop/ と version.py へのリード権限を Claude Code へ委譲。B-003 は互換影響の事前調査・報告が条件。
- 連携: 以後の push・マージは保持したデバイス認証トークンでリード（Muse）が随時実施する（オーナー承認済み）。

## 2026-11-17 — B-001 ベースライン確立（Claude Code、クラウド）
- オーナー判断で本結果を正式なベースラインとして採用（Muse 側のビルドは停止）。詳細: ai_loop/iterations/2026-11-17-B-001-claude.md
- ビルド設定: AGENTS.md の規定どおり `scons platform=linuxbsd target=template_release tests=yes accesskit=no arch=x86_64`（並列数だけ -j4。4コアのため）。対象 ai-native @ 3d70ab368。
- マシン: Claude Code クラウドコンテナ（gVisor）、Xeon 2.10GHz × 4コア、RAM 15GB、Ubuntu 24.04.5、gcc 13.3.0、Python 3.13.18、SCons 4.11.1、zstandard 0.25.0。
- 所要時間: フルビルド 15分54秒（2,665 ファイル、exit 0）、単体テスト約31秒。
- テスト: 総数 1431／pass 1430／fail 1／skip 3（assertion 423591 中 1000 が fail）。
- fail: `[IP] resolve_hostname`（tests/core/io/test_ip.cpp:44）、localhost の IPv6 解決が空になる。環境起因と判断した根拠: コンテナの /etc/hosts に `::1 localhost` が無く、getent でも Python の getaddrinfo でも AF_INET6 の localhost を引けない。`::1 localhost` を一時的に足すと全 1431 件 pass、fail 0 になる（そのあと元に戻した）。コード起因の失敗は 0 件。
