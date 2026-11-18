# 灯りの村 — 生成画像を使ったゲーム内 UI 見本

画像生成で作った藍色の会話枠と夕暮れの村を、Godot の画面に組み込んだ独立した見本。枠は `StyleBoxTexture` の9分割で、画面幅を変えても角飾りを伸ばさない。日本語の本文、進行ボタン、フォーカスは Godot の `Control` で表示する。背景画像はこの見本の表示用。

```sh
sample=ai_loop/look_samples/lantern_ui
godot --headless --path "$sample" --import
godot --path "$sample"
```

「次へ」で会話を送り、「閉じる」で閉じる。「会話を開く」で最初から表示する。Tab でボタンにフォーカスを移し、Enter で操作できる。

描画できる環境で、横長と縦長を検証する。PNG と操作結果の JSON を出す。

```sh
godot --path "$sample" --resolution 1152x648 --script "$PWD/$sample/verify.gd" -- /tmp/lantern-wide.png
godot --path "$sample" --resolution 390x844 --script "$PWD/$sample/verify.gd" -- /tmp/lantern-portrait.png
```

`art/dialog.webp` と `art/village.webp` はこの見本用に画像生成した素材。`lantern_*.woff2` は既存の `ai_loop/ui_font.py` で切り出した日本語と欧文のフォントで、出典とライセンスは隣の `*.LICENSE.txt` にある。台詞やラベルを変更して文字を追加する場合はフォントを再サブセットする。
