# B-727 の水面を GPU で比較する

リポジトリのルートで次を実行する。出力先は新規ディレクトリを指定する。

```powershell
python ai_loop/windows/water_gpu.py ../water-output
```

使う editor バイナリは環境変数 `GODOT` で指定する。未指定なら
`bin/godot.windows.editor.x86_64.console.exe` を使う。B-727 を含む editor
バイナリと、描画できる画面が必要。ソフトウェア Vulkan（Mesa llvmpipe）
でも手順の確認はできるが、時間の値は試走用。

水の見本（`ai_loop/look_samples/water`）を一時ディレクトリにコピーし、
同じカメラとライティングで、描画方式（Forward+／Mobile／Compatibility）
× ケース（水なし／水あり／画面空間反射なし／屈折なし）を撮る。

```powershell
python ai_loop/windows/water_gpu.py ../water-output --renderers forward_plus,mobile,compatibility --cases none,water,no_reflection,no_refraction --frames 300 --resolution 1920x1080
```

短い試走は `--frames 12 --resolution 640x360`。出力は描画方式とケースごと
の `<renderer>_<case>.png` と、標準出力の JSON 1 行
`{"ok", "results": [{"renderer", "case", "gpu_ms_median", "gpu_ms_p95", ...}]}`。
各ケースには、同じ描画方式の水なし画像との比較で測った `water_px`（水の
ある画素数）、`foam_px`（泡の画素数）、浅瀬と深部の平均 RGB（`near`／`far`）
が付く。

水の動きには実行時刻が影響する。画像は、浅瀬と深い所の色、岸の泡、底の帯
の屈折、奥の塔や杭の反射を目視で比較する。フレーム時間はシーン全体の描画
を含むので、差は環境や実行順で揺れる。同じ実機と解像度で複数回測り、中央
値の差を照合する。描画エラー、無効な計測値、タイムアウトで終了コード 1
になり、その時点までの結果とログを残す。GPU リセットで描画を失うことが
ある機材では、1 ショットにつき最大 2 回まで撮り直す。

Python 3.9 以降と Pillow が必要。
