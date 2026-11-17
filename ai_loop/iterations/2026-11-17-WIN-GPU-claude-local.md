# WIN-GPU: オーナーの PC（GPU）での確認と計測 — Claude Code（ローカル）

- 環境: Windows 11 Home、NVIDIA GeForce RTX 4050 Laptop GPU（ドライバ 617.42）、Vulkan 1.4.351、MSVC 14.5、12 スレッド。
- ビルド: `scons platform=windows target=editor tests=yes d3d12=no accesskit=no -j11`（初回 約22分）。コミット b99fe81df2 時点。
- 単体テスト: 1470 pass／0 fail／3 skipped（アサーション 530,435）。クラウド（1476）との差6件は未調査。
- 追加した道具: `ai_loop/windows/shoot_gpu.py`、`frame_time.gd`、`look_variant.gd`（使い方は `ai_loop/windows/README.md`）。

## 画質ごとの GPU フレーム時間（1920×1061、垂直同期オフ、300 フレームの中央値、Clear）

| 見本 | なし | 軽量 | 標準 | 高 |
|---|---|---|---|---|
| landscape | 0.44 ms | 0.96 | 1.17 | 3.17 |
| street | 0.54 ms | 1.06 | 1.38 | 3.61 |

## 高画質の内訳（street、高から1つだけ外したときの減り方）

| 外したもの | GPU ms | 差 |
|---|---|---|
| （基準：高） | 3.59 | — |
| SSIL | 2.86 | −0.73 |
| SSR | 3.19 | −0.40 |
| 柔らかい影（light_angular_distance 0.5→0） | 3.19 | −0.40 |
| SSAO | 3.24 | −0.35 |
| 影の分割の混ぜ合わせ（blend_splits） | 3.36 | −0.23 |

標準→高の +2.2 ms は、ほぼ SSIL＋SSR＋柔らかい影＋blend_splits の合計。

## 見つけた問題: 高画質の柔らかい影がザラザラする

- 見本 street（apply＋rig、apply＋telephoto）の手前の地面で、影の境目が粒状の模様になる。60 フレーム描いても消えない。
- 原因: 太陽の `light_angular_distance = 0.5`（PCSS）。0 にすると消える。
- 粒の量（隣り合う画素の明るさの差の平均。手前の地面 700×170 px）と重さ:

| 設定 | 粒の量 | GPU ms |
|---|---|---|
| 高（今のまま） | 4.93 | 3.59 |
| 柔らかい影なし | 0.28 | 3.19 |
| angular 0.25 | 2.85 | 3.50 |
| shadow_blur 2.0 | 5.82 | 3.76 |
| 柔らかい影のフィルタ High | 2.28 | 4.33 |
| 柔らかい影のフィルタ Ultra | 1.89 | 4.85 |
| angular 0.25＋フィルタ High | 1.37 | 4.33 |
| TAA を有効 | 1.33 | 4.17 |

- 案: (a) 高画質で TAA を使う前提にする（+0.6 ms、動きの残像に注意）、(b) angular を 0.25 に下げてフィルタ High（+0.7 ms、影の柔らかさは控えめになる）。どちらにするかは見た目の判断なので、オーナーかクラウドの担当に任せる。
- 未確認: エディタでのボタンと構図ガイドの表示（画面操作の許可がなかったため、オーナーが目で見る）。B-703b（UZUPostEffect）の GPU での表示は、エディタを閉じてから再ビルドして確かめる。
