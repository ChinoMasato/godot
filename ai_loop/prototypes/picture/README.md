# U.ZU Picture（試作）— 一枚絵を、動く 3D の場面にする

> **このフォルダには絵が入っていない。** `picture.json` が指す `art/`（イケベロスのタイトル絵・深度・マスク）と、比較に使った Poly Haven の素材は、公開リポジトリなので入れていない。自分の絵と深度（disparity でも linear でも可）を `art/` に置けば動く。`map estimate` を使うときは `estimators.example.json` を `estimators.json` に写して、自分のモデルのコマンドに書き換える。

一枚絵と奥行きの画像から、起伏のある 3D の面を作る。そこに本物のライト、雨、火の粉、霧、カメラの揺れを加え、U.ZU ルックで仕上げる。
**操作するのは AI エージェント**。設定はすべて `picture.json` に書き、位置は絵のピクセル座標（x は右、y は下）で指定する。

## エージェントの手順

```sh
python picture.py <project> schema                 # 書けるキーの説明
python picture.py <project> analyze --overlay res://overlay.png
#   光源の候補（ピクセル位置、色、奥行き、印の色）。overlay.png を見て、本物の光源か確かめる。
#   濡れた床の照り返しも候補に入るので、絵を見て除く。
python picture.py <project> snap --at "1060,560;1630,540"
#   目で見た大まかな点を、近くでいちばん明るい点に合わせる（40px 以内）。
python picture.py <project> adopt --ids 0,3 --at "1060,560"
#   候補の番号と大まかな点を、そのまま spec の lights に書く（ほかのキーは残す）。
python picture.py <project> validate               # ファイルの有無、絵の外の点、知らないキーを全部挙げる
python picture.py <project> shot --out shots --seconds 0,6
#   GPU で描画して、指定した秒の PNG と、最初と最後を重ねた motion.png を作る。見て直して、また撮る。
```

すべてのコマンドは JSON を1行だけ返す（`ok`、`problems`、`next` など）。

## 必要な素材

| キー | 必須 | 作り方 |
|---|---|---|
| `image` | ○ | 一枚絵 |
| `depth` | ○ | 奥行き。AI モデル（Depth Anything V2 Small＝Apache-2.0 など）で推定する。白が近い（disparity） |
| `normal` | － | 法線マップ。無ければ、本物のライトの当たり方が平らになる |
| `regions` | － | 白黒のマスク画像と、そのチャンネル。glow（光る）、wet（濡れて光る）、sky（雲が流れる）、flicker（火）、waterfall、sway（旗・葉が揺れる）、water |

## 仕組み

- 起伏の面: 奥行きの画像から 360×204 の格子を作り、各点を、カメラから見たその画素の方向の、推定した距離に置く。
- PBR: albedo は絵、法線は法線マップ、roughness は wet の領域で下げる。絵に描かれた光は emission で残し、本物のライト（OmniLight3D）の光と照り返しを上に足す。
- 効果: 雨と火の粉は GPUParticles3D。霧と光のにじみは U.ZU ルックの Environment。縁の強調と周辺減光は UZUPostEffect。
- カメラ: 横に揺れながら少しずつ寄るので、近くと遠くがずれて奥行きが見える。

## 試作の状態（2026-11-18）

- イケベロスのタイトル絵で動いた（RTX 4050、エラー0件）。絵は `art/` にあるが、**エンジンのリポジトリには入れない**。
- 既知の問題: 0秒の時点で、火の粉が固まって赤くにじむ。左半分が元の絵より少し暗い。
- エンジンに組み込むときは、`uzu_picture.gd` を C++ のノード（`UZUPicture`）に、`picture_tool.gd` を `ai_loop` のコマンドにする。

## テクスチャ画像 → PBR テクスチャ

1枚のテクスチャ画像（色だけ）から、PBR の一式を推定して作る。**推定であって実測（スキャン）ではない。**

```sh
python picture.py <project> pbr kinds                      # 素材の種類と既定値
python picture.py <project> pbr make --in res://tex.png --out res://materials/stone --kind cobble --tile
#   albedo.png（焼き込まれた明暗を除いた色）、normal.png（OpenGL 形式、Godot 向け）、
#   orm.png（R=AO、G=roughness、B=metallic）、height.png、material.tres（ORMMaterial3D）を書く。
#   返す JSON: stats（roughness の平均、法線の強さ、焼き込まれた明暗の大きさ、継ぎ目）と warnings。
python picture.py <project> pbr preview --dirs res://materials/stone,res://materials/brick --out res://preview.png
#   低い斜めの光で、球とタイルした床に貼って撮る（GPU）。凹凸・ツヤ・継ぎ目を目で確かめる。
python picture.py <project> pbr compare --normal res://a/normal.png --ref res://scan_nor_gl.png --rough res://a/orm.png --ref-rough res://scan_arm.png
#   本物の素材と数値で比べる（法線の角度の差、roughness の相関）。
```

- 調整のつまみ: `--kind`（stone, brick, cobble, ground, wood, plaster, fabric, leather, metal, painted_metal）、`--strength`（凹凸の強さの倍率）、`--delight`（明暗を消す強さ 0〜1）、`--tile`（端をつないで計算）、`--invert`（暗い所を高くする）、`--size`。
- 仕組み: 明るさのうち、ゆっくり変わる成分を「焼き込まれた光」とみなして色から除き、細かい成分を高さにする。高さの傾きから法線、周りより低い所を AO、凹みを粗く、を作る。

### 本物のスキャン素材との比較（2026-11-18、Poly Haven CC0、RTX 4050）

| 素材 | 法線の角度の差（作った物） | 平らな法線の場合 | roughness の相関 |
|---|---|---|---|
| 石畳 cobblestone_floor_08 | 14.2° | 14.9° | +0.64 |
| レンガ castle_brick_broken_06 | 18.6° | 18.1° | −0.49 |
| 岩場 rocky_terrain_02 | 9.1° | 8.4° | +0.09 |
| 草と岩 aerial_grass_rock | 10.7° | 9.5° | +0.38 |

- 正直な評価: 法線の向きの正確さは「平ら」とほぼ同じ。明るさから形を当てる方法の限界で、色の濃い汚れを穴と間違える。見た目では凹凸がはっきり出て、石畳では本物に近く見える（previews/both.png）。
- 強さの既定値は、上の比較で合わせた（最初の値は約4倍強すぎて、平らより悪かった）。
- レンガの roughness は向きが逆になった。目地（暗い）を粗くする前提が、この素材では合わない。
- 形を正しく当てるには、画像から法線や高さを推定する AI モデル、または生成時に高さの画像も作る方法が要る（エンジンの外の道具）。

## 外の AI モデルの法線・奥行きを使う（map）

エンジンは形を自分で当てない。エージェントが好きなモデル（Depth Anything、法線推定モデル、画像生成で作った高さ画像など）で作った画像を受け取り、**使える形に直す**。

```sh
python picture.py <project> map import --kind normal --in res://model_normal.png --for res://tex.png --out res://normal.png
python picture.py <project> map import --kind depth  --in res://model_depth.png  --for res://art.png --out res://depth.png [--depth-kind auto|disparity|linear]
python picture.py <project> map estimate --kind depth --in res://art.png --out res://depth.png   # estimators.json のコマンドを実行してから import
python picture.py <project> pbr make --in res://tex.png --out res://materials/x --kind cobble --normal-from res://normal.png
```

### 向きが分かっているモデルを使うとき（B-903 補完）

```sh
# モデルがDirectX形式の法線を返すと分かっている場合
python picture.py <project> map import --kind normal --normal-convention directx --in res://model_normal.png --for res://tex.png --out res://normal.png
# モデルが白＝遠い深度を返すと分かっている場合
python picture.py <project> map import --kind depth --white-is far --in res://model_depth.png --for res://art.png --out res://depth.png
```

- `--normal-convention auto|opengl|directx`、`--white-is auto|near|far` を選べる。既定はauto。既知の形式を指定した場合は推測より優先し、JSONの `convention_source`／`direction_source` にexplicitを返す。
- 自動判定で画像の上下から近遠を判断できないとき、向きを変えず警告する。`found.white_is` と `output_white_is` はunsureになり、既知の形式を指定して再実行できる。一定値の深度も、その値を保ったまま警告する。
- 互換用の `--depth-kind disparity` は白＝近い、`linear` は白＝遠いという向きの指定として使える。これは画素の範囲合わせと反転であり、メートル単位の線形距離を物理的な逆距離へ変換する処理ではない。新しい指定では `--white-is` を推奨する。矛盾する指定やタイプミスはエラーにする。
- 元の画像・元のマップを出力先に指定するとエラー。出力はPNG限定で、ディレクトリ作成とPNG保存の結果も検査する。
- `map estimate` は毎回別のraw画像パスを外部モデルへ渡す。出力画像を作らなかった場合に、前回のraw画像を再利用しない。raw画像のパスは成功時の `raw` に返す。向きの指定と `--no-align` はimport段階へ引き継ぐ。
- 外部モデルの失敗時は既存の最終出力を維持する。コマンド設定は空でない文字列で、`{in}`／`{out}` の両方が必要。

実エンジンを使う回帰試験（合成画像のみ、GPU不要。`GODOT`でエディタを指定）:

```sh
python -m unittest discover -s ai_loop/prototypes/picture -p test_map_import.py -v
```

- `estimators.json` の例: `{"depth": "python my_depth.py {in} {out}", "normal": "python my_normal.py {in} {out}"}`（`{in}`/`{out}` は絶対パスに置き換わる）。
- import が調べて直すこと（結果の JSON の `found`／`changed`／`warnings` に全部書く）:
  - 大きさ: 元の絵に合わせて拡大縮小。縦横比が違えば、切り取りや余白の疑いを警告。
  - ずれ: 元の絵の輪郭と比べて、数ピクセルのずれを見つけて直す（確かなときだけ）。
  - 法線の緑の向き（OpenGL／DirectX）: 元の絵の明暗の傾きと比べて判断し、逆なら反転。判断できないときは「unsure」として何もしない。長さを1にそろえる。青が低ければ「タンジェント空間でない」と警告。
  - 奥行きの向き: 下（地面）が上（空）より近いかで、白が近いか遠いかを判断し、白＝近いにそろえる。幅が狭ければ 0〜1 に広げる。

### 試験（2026-11-18）

| 入力 | 結果 |
|---|---|
| 正しい法線（石畳・岩場） | OpenGL と判断、反転なし |
| 緑を反転した法線（石畳・岩場） | DirectX と判断して反転 |
| レンガの法線 | 「判断できない」と警告し、反転しない（明暗と形の関係が弱い素材） |
| 24px ずらした法線 | 石畳・レンガは 24px を見つけて戻した。岩場（2048px）は 16px（解析の目が粗い） |
| 奥行き（正しい／白黒反転／幅が狭い／正方形） | すべて元の奥行きと一致（相関 1.0） |
| DirectX の法線 → import → pbr make --normal-from | スキャンとの差 0.44°（明るさからの推定は 14.2°） |
