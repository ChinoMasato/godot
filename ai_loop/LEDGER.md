# LEDGER — 検証ループの実行記録

失敗も消さない。1イテレーション = 1行ブロック。

## 2026-11-17 プロジェクト開始

- フォーク点: Godot master @ 203bf0a（4.8 beta）。ブランチ `ai-native` を作成。
- 環境実測: clone 約2分／477MB。SCons は venv（~/workspace/.venv-build）に導入。
- ビルドプローブ: template_release で4分間に178オブジェクト（約44個/分）。全対象は約2,000ファイル、フルビルド推定1〜1.5時間。
- ベースラインビルド（template_release, tests=yes）をバックグラウンドで開始。結果は完了時にここへ追記する。
