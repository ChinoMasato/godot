# B-005 Windows 起動アイコン補完（2026-11-18、Codex local）

オーナーから「U.ZU の前に Godot のロゴが出る」「アプリアイコンも」と報告。

## 原因と変更

- `DisplayServerWindows` はウィンドウ作成時に PE の `GODOT_ICON` を使う。後から適用する `main/app_icon.png` とは別の画像。
- 前回の B-005 は Windows 固有 `.ico` を対象外としており、通常 exe の `godot.ico` と console ランチャーの `godot_console.ico` が Godot のままだった。
- この2ファイルを、既存の公式 U.ZU ラスター `misc/logo/icon.png` から生成。16/20/24/32/40/48/64/96/128/256px。
- `platform/windows/SCsub` にそれぞれの ICO の明示的依存を追加し、画像だけ変えた増分ビルドでも resource が更新されるようにした。
- `misc/logo/uzu/windows_icons.py` は生成と PE 資源の検査を行う。Windows API のデータ読み込みモードで exe を開き、アイコングループ全サイズを比較する。検証で exe 自体を起動しない。

## 検証

- 旧 exe: 既存6サイズの native icon は U.ZU と不一致。一方、埋め込みの起動 splash PNG と runtime icon PNG は既に現在の U.ZU と一致していた。上流の splash/app_icon PNG はバイナリ内で見つからなかった。
- ICO を再生成しても SHA256 が一致（決定的な変換）。256px の元画像と ICO のRGBA画素が完全一致。
- 修正コミット b4ba875871 をローカル checkout で一時的にビルド。成功、1分37秒。任意の ANGLE SDK が無いという既存の警告のみ。
- 通常 exe: native icon 全10サイズ一致、runtime splash PNG一致、runtime app icon PNG一致。
- console exe: native icon 全10サイズ一致。console ランチャーには runtime PNG は含まれない（期待どおり）。
- ローカルにはキャッシュの影響を切り分けるため、同じ実行ファイルの `bin/uzu-engine.windows.editor.x86_64.exe` も作成。元の exe とSHA256完全一致。通常の `godot.windows.editor.x86_64.exe` も更新済み。
- ビルド後、メイン checkout のブランチは ai-native に復帰。修正ソースの統合はリード担当。統合前に ai-native を再ビルドすると旧 ICO に戻るため、その間は上の U.ZU 名の exe を使える。

- 修正版で全テスト **1487 passed／0 failed／3 skipped**、530949アサーション成功。

## 確認の範囲

今回確認したのは画像資源とバイナリ。起動直後から閉じるまでの画面全体の録画・目視検査は未実施。前に起きた Vulkan device loss の再試行は行わず、アイコン検証は描画なしで実施した。
