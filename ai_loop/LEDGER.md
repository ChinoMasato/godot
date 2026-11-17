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

## 2026-11-17 — リード統合: Claude ブランチを ai-native へマージ
- claude/uzu-engine-briefing-rrno7b（3bb7ea0）を ai-native へ fast-forward マージ。内容: B-001 詳細記録（LEDGER 本編＋iterations）、B-002 measure.sh（リードがレビュー: flock 直列化・失敗時も記録・doctest 集計のパースを確認、bash -n pass）、B-003 事前調査（コード変更なし）。
- Q1 の条件（ベースライン詳細記録）はこれで充足。B-001 は正式に完了。
- B-003 の実装は Q3（COORDINATION.md）のオーナー決定待ち: short_name の扱い（案A: name のみ変更／案B: 両方変更）、判定条件の差し替え、measure.sh の TZ 既定。

## 2026-11-17 — Q3 決定（オーナー承認）: B-003 は案Aパッケージ
- B-003 の範囲を version.py の name のみに確定（short_name は "godot" のまま、互換維持）。BACKLOG の判定を「起動ヘッダの U.ZU Engine 表示＋全テスト pass」に差し替え（--version は名前を含まないため）。
- measure.sh の日付は TZ=Asia/Tokyo を既定化（リードが適用、bash -n pass）。
- B-003 の実装は Claude Code が担当ブランチで実施し、検証（起動ヘッダ＋全テスト）つきで申告する。

## 2026-11-17 — AIファースト改善マップを反映（オーナー指示）
- フォーク実物の棚卸しに基づく改善マップを設計書（第2節末「AIファースト改善マップ」）と BACKLOG へ反映。不足は「出力の構造化」「合否判定」「プレイ自己検証」の3点に集約。原則: 追加モード／オプトインのみで既存挙動を変えない。
- Wave 1 として5件を登録: B-104（ベンチ合否）、B-201（構造化エラーログ試験実装）、B-202（GDScriptエラーJSON）、B-203（プロジェクト検証コマンド）、B-303（自己検証プレイ最小版）。残り候補は BACKLOG の保留へ Wave 2 として集約。

## 2026-11-17 — リード統合: B-003 実装を ai-native へマージ
- version.py の name が "U.ZU Engine" になった（short_name は "godot" のまま）。新規テスト tests/core/test_version.cpp（名前と互換を固定する2件）を含む。
- 検証（Claude Code の最終検証をリードが確認）: 最小プロジェクトで `--headless --quit` の起動ヘッダが "U.ZU Engine v4.8.beta..." と表示され exit 0。全テスト 1433 件中 1432 pass（fail は既知の環境起因 [IP] resolve_hostname のみ。::1 追加時は全件 pass）。変更前は新規テストが fail することも確認済み（red→green）。
- BACKLOG の B-001／B-002／B-003 を完了に更新。Phase 0 は完了。
- 申し送り: 起動ヘッダの website（version.py の website = godotengine.org）の扱いは未決定。必要なら別項目としてオーナー判断。
