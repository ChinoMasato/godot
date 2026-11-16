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
