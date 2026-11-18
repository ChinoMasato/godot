# B-108（起動直後に Godot のアイコンが出る）の確認 — Claude Code ローカル（Windows、2026-11-18）

オーナーから再報告:「タスクバーのアイコンが、最初に Godot のアイコンが出てから U.ZU に切り替わる」。

## 状況

- 修正は Codex のブランチ `agent/b-005-codex-windows`（b4ba875871、08332d35bd）にすでにある。中身は `platform/windows/godot.ico` と `godot_console.ico` を U.ZU（16〜256px の 10 サイズ）に差し替え、`platform/windows/SCsub` に ICO への依存を足す（画像だけ変えた差分ビルドでも資源が作り直される）。生成と検査は `misc/logo/uzu/windows_icons.py`。
- **ai-native にはまだ入っていない**（`git merge-base --is-ancestor` で確認）。ai-native からビルドし直すと、実行ファイルに埋め込まれたアイコンは Godot に戻る。オーナーの報告は、この状態のビルドで起きた。

## ローカルでの確認

- ai-native 0fafda451f 以降の作業ツリーに、上のブランチの 4 ファイル（2 つの ICO、SCsub、windows_icons.py）だけを一時的に当ててビルド（4 分 51 秒、成功）。
- `windows_icons.py --verify-exe`:
  - 通常の exe: 埋め込みアイコン 10 サイズすべて一致、起動画面と実行時アイコンも一致（ok）。
  - コンソール版の exe: 埋め込みアイコン 10 サイズすべて一致（ok）。
- Windows のシェルが表示するアイコン（`ExtractAssociatedIcon`）も、2 つの exe とも U.ZU。
- 確認の後、ソースは ai-native に戻した（作業ツリーの差分なし）。`bin/` の実行ファイルは U.ZU のアイコン入りのまま。次に ai-native からビルドし直すと、統合されるまでは Godot に戻る。
- **タスクバーの実画面でも確認した**（オーナーの依頼で起動）。エディタを `-e --path <見本>` で起動し、タスクバーを約 0.3 秒おきに撮影。アイコンが現れた最初のコマ（+0.3 秒）から U.ZU で、Godot のアイコンは一度も写らなかった（`ai_loop/look_samples/compare/b108_taskbar_launch_windows.png`）。0.3 秒より短い表示は撮影間隔の外だが、最初に出るアイコンは exe に埋め込まれたもので、それが U.ZU であることは上で確認済み。Windows のアイコンキャッシュが古いアイコンを出す場合は、ピン留めし直すか、エクスプローラーを再起動すると更新される。

## リードへのお願い

- `agent/b-005-codex-windows` の統合で B-108 は直る（Windows）。統合後に GPU 不要の確認として `windows_icons.py --verify-exe` を回せる。
- B-108 の「macOS の icns の生成元も同じか確認」: **macOS 用の icns はすべて上流の Godot のまま**（`misc/dist/macos_tools.app/Contents/Resources/GodotLG.icns`（エディタ）、`misc/dist/macos_template.app/Contents/Resources/icon.icns`（書き出しテンプレート）、ほか文書型アイコン）。最終変更はどれも上流のコミット。macOS でビルドする予定ができたら、同じ公式 U.ZU 画像から作り直す必要がある。
