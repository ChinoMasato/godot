# テスト総数の環境差（クラウド Linux／ローカル Windows）の切り分け — Claude Code ローカル（2026-11-18）

リードの依頼（COORDINATION「リードの修正指示」）: クラウド 1491／Windows 1485 などの差が、環境によるものか実際の差かを切り分ける。

## 結論: 環境による差。実際の差はない

- 差の正体は **`tests/core/io/test_uds_server.cpp` の 6 件**（UDSServer、Unix ドメインソケット）。ファイル全体が `#ifdef UNIX_ENABLED` の中にあり、Windows ではコンパイルされない。
- 突き合わせ: Windows のバイナリ（下記）は 1487 件。同じ内容のコミット 8d781aa9ee 時点でクラウドが報告した件数は 1493 件（B-720 の記録）。差は **6 件で、UDSServer の 6 件と一致**する。Windows の一覧には UDSServer が 1 件もない。
- 逆向き（Windows でだけ増えるテスト）はない。テスト中の `#ifdef WINDOWS_ENABLED` は、すべてテストの中身（パスや環境変数の書き方）を分けるだけで、件数は変えない（test_project_settings、test_config_file、test_os）。
- モジュールの抜けもない。modules/*/tests のテストはすべて Windows の一覧にある。dds の 2 件は `doctest::skip(true)` で、両方の環境とも skip（skip の 3 件は、この 2 件と `Pending tests are skipped`）。
- `#if 0`（Dictionary の再帰キー 4 件、Geometry3D の Build Convex Mesh）と `#if false`（NavigationServer3D の非同期ベイク）は、両方の環境でコンパイルされない。
- 今後の目安: **Windows の件数 ＝ クラウドの件数 − 6**。これ以外の差が出たら、実際の差として調べる。

## 使ったバイナリについての注意

- 調べたバイナリは `4.8.beta.custom_build.b4ba87587` だった。これはローカルの共有チェックアウトの `bin/` を Codex が B-005（Windows アイコン）のブランチでビルドしたもの。中身は ai-native 8d781aa9ee＋アイコン差し替えで、テストの変更はない。
- ローカルでは Claude Code と Codex が同じ `bin/` を使っている。GPU の計測をするときは、`--version` でバイナリのコミットを先に確かめる。

## 置いたもの

- `ai_loop/test_cases_diff.py`: `list <binary>` でテスト名の一覧（並べ替え済み）を出す。`diff a.txt b.txt` で、片方にだけある名前を JSON で出す。
- `ai_loop/windows/test_cases_windows.txt`: 上のバイナリの一覧（1487 件）。クラウドでも `list` を取って `diff` すれば、UDSServer の 6 件だけが出るはず。
