# B-725 の GPU 再確認 — Claude Code ローカル（Windows、RTX 4050、2026-11-18）

Codex の `agent/b-725-codex`（7368ae68a5、人物を専用レイヤー 20 に置き、BlobShadow の Decal の cull_mask から外す）を手元で一時的に当ててビルドし、見本 hd2d に `look hd2d` をかけて Vulkan で撮った。撮影後、手元の作業ツリーは ai-native に戻した（このブランチにエンジンの変更はない）。

## 結論

- **胴の縦の帯は消えた（判定の前半は OK）。** 手前の人物（Hero3）の胴で、列ごとの明るさの最大と最小の差は、B-725 前の **30.5** から **5.4** になった。これは BlobShadow を外したときと同じ値。保存された場面でも、人物は layers 524288（レイヤー 20）、Decal の cull_mask は 524287 だった。
- **ただし、地面の丸い影が出ていない（判定の後半は未達）。** BlobShadow ありとなしの画像は、ピクセル単位で完全に一致した（差分なし）。B-725 の前も、差が出ていたのは人物の板の上だけで、地面には一度も乗っていなかった。
- **原因（B-716 の配置の問題で、B-725 のせいではない）:** `UZULook::add_blob_shadow` は大きさと位置を `get_aabb()` から決めている。しかし、ヘッドレスの `look hd2d` の中では Sprite3D の AABB が空（P (0,0,0)、S (0,0,0)）。そのため:
  - 半径が下限の 0.1 m になる（保存された `size = Vector3(0.2, 1, 0.2)`）。
  - 位置は足元ではなく板の中心のまま（transform なし）。人物は y=0.72 にいるので、高さ 1 m の箱（y 0.22〜1.22）は地面（y=0）に届かず、胴だけを含んでいた。
  - 帯の正体はこの「胴の位置にある小さな Decal」だった。B-725 で人物に乗らなくなったので、いまは何にも映っていない。

## 直し方の案（クラウド担当の scene/3d/uzu_look.cpp）

AABB ではなく、テクスチャの大きさ × pixel_size（または `get_item_rect()` × pixel_size）から大きさを出し、Decal を足元に置く。ヘッドレスでも決まる値で、texel の正規化（B-715）の後に計算する。

```cpp
const Rect2 rect = sprite->get_item_rect();               // ピクセル単位、ヘッドレスでも有効
const float px = sprite->get_pixel_size();
const float radius = MAX(rect.size.x * px * 0.4f, 0.1f);
decal->set_position(Vector3(0.0, (rect.position.y) * px, 0.0)); // 中心揃えなら -rect.size.y * px * 0.5
```

（`get_item_rect()` は centered=true のとき position.y = -size.y/2。y 方向の符号は Sprite3D の向き（flip、axis）に合わせて確認が必要。）

この配置を撮影用の道具の中で試した結果（エンジンは B-725 のまま、Decal の size と position だけを書き換え）:

| 状態 | 胴の帯（列の明るさの差） | BlobShadow なしとの差分 |
|---|---|---|
| B-725 前 | 30.5 | 人物の板の上だけ |
| B-725 | 5.4 | なし（完全一致） |
| B-725 ＋ 足元に置く案（半径 0.384 m、y −0.72） | 5.4 | 足元の地面（bbox 758,591–1143,770） |

足元に置くと、帯は出ないまま、3 人の足元の地面に柔らかい丸い影が出た。

## テストの案

ヘッドレスのテストで AABB が空でも、`look hd2d` の後に BlobShadow の y 位置が負（足元側）になり、大きさが texture 幅 × pixel_size × 0.8 になることを確かめる（いまのテストは Decal があることと cull_mask しか見ていない）。

## 撮影について

- 道具: `ai_loop/windows/sprite_shadow_variant.gd`（ローカルで変種 `blob_at_feet` を足して撮影。この変種はリポジトリには入れていない）。
- 画像: `ai_loop/look_samples/compare/b725_gpu_windows.png`（左から: B-725 前、B-725、B-725 ＋足元に置く案）。
- 今回の撮影では GPU のリセットは起きなかった。
