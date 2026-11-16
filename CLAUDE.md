# CLAUDE.md — U.ZU Engine

@AGENTS.md

## Claude Code で作業するとき

- このリポジトリのコード開発は Claude Code を主ツールとして行う（千野の決定、2026-11-17）。Codex など他のエージェントも同じ規則で参加できる。
- 作業前に必ず `ai_loop/MULTI_AGENT.md` を読む。複数エージェントが同時に開発するための手順がそこにある。
- **メインチェックアウト（ai-native）では直接編集しない。** 必ず自分の担当イテレーション専用の worktree で作業する（`ai_loop/agent_start.sh <BACKLOG-ID> <名前>` で作る）。
- 担当は BACKLOG の1項目だけ。着手前に `ai_loop/CLAIMS.md` へ担当と触るパスを宣言する。
- 探索（コード調査・影響範囲の洗い出し）はサブエージェントで並行化してよい。ただし編集は自分の worktree 内・自分の宣言範囲だけ。サブエージェントに別の worktree を持たせない。
- ビルドとテストは `ai_loop/build_locked.sh` 経由（同時ビルド禁止のロック付き）。
- 終了時は `ai_loop/agent_finish.sh` で全テスト pass を確認し、`ai_loop/iterations/` に自分の記録を1ファイルで残す。`ai-native` へのマージと `LEDGER.md` への追記は統合担当（リード）だけが行う。
