# 描画ルック調査: IKEBEROTH（黄昏六号館）→ U.ZU Engine — claude (2026-11-17)

- 種別: 調査のみ（コード変更なし）。BACKLOG 項目の提案の根拠資料（COORDINATION.md Q4）。
- 対象: オーナーが Claude Code で開発した IKEBEROTH（https://u-zu.co.jp/tasogare-rokugokan/）。three.js r186、WebGL2。公開ページから配信されている JavaScript を読んだ。
- 方針: このリポジトリは公開なので、IKEBEROTH のコードやシェーダーは転記しない。使っている技術と、主な設定値だけを記す。

## きっかけ

オーナーの評価: 「Godot の 3D の光と影は three.js 以下に見える。IKEBEROTH の描画ノウハウくらいまでは行きたい」。

## 原因（このリポジトリのコードで確認）

`scene/resources/environment.h` の初期値は、トーンマップ `TONE_MAPPER_LINEAR`、`ssao_enabled = false`、`ssil_enabled = false`、`sdfgi_enabled = false`、`glow_enabled = false`、`fog_enabled = false`、`volumetric_fog_enabled = false`。`camera_attributes.h` も `dof_blur_far_enabled = false`、`auto_exposure_enabled = false`。
つまり、光と影の効果はすべて初期状態でオフになっている。エディタの仮の空と太陽もプレビュー専用で、ゲームの実行時には入らない。

## IKEBEROTH の描画の構成と、Godot 4.8（Forward+）での対応

| IKEBEROTH | Godot での対応 | 種類 |
|---|---|---|
| ACES トーンマップ、露出 0.95〜1.05 | Environment `tonemap_mode = ACES`（AgX も選べる）、`tonemap_exposure` | 設定 |
| HDRI の空 → PMREM で作った環境光（強さ 0.24〜0.34） | Sky＋PanoramaSkyMaterial（HDR）、環境光と反射光の光源を Sky、環境光の強さ | 設定 |
| 太陽1灯の DirectionalLight、PCF ソフトシャドウ、影の解像度 4096／2048／512（画質で切替）、プレイヤー追従、テクセルにスナップ | DirectionalLight3D、PSSM 4分割（段階分けした影）、影のぼかし、`rendering/lights_and_shadows/directional_shadow/size` とソフトシャドウ品質 | 設定（追従は PSSM が自動でやる） |
| HemisphereLight（空色と地面色）＋影なしの補助 DirectionalLight | 空からの環境光で代用。補助光は影なしの DirectionalLight3D | 設定 |
| FogExp2（指数フォグ） | Environment の指数フォグ。必要に応じて高さフォグ、ボリュームフォグ | 設定 |
| GTAO（半解像度、8サンプル、範囲120m）。画質「高」だけ有効 | SSAO＋SSIL。画質で品質を切り替える | 設定 |
| UnrealBloom（強さ 0.36〜0.46、しきい値 0.85〜1.15） | Glow（`glow_hdr_threshold`、レベルごとの強さ、ブレンド） | 設定 |
| SMAA（画質「軽量」以外で有効） | Viewport `screen_space_aa = SMAA`（TAA、MSAA も可） | 設定 |
| 32³ の LUT による色調整（青い影、中立の石、控えめな金のハイライト、S字カーブ） | Environment `adjustment_color_correction` に Texture3D を指定 | 設定＋LUT の素材 |
| 独自の後処理: 周辺減光、ハイライトを暖色に・暗部を青寄りに、軽いシャープ化、細かい粒状ノイズ | 画面全体にかけるシェーダー（screen texture を読む）、または CompositorEffect | シェーダー |
| 深度を使った後処理: 遠景だけをぼかす、空気遠近、太陽方向の光芒（戦闘中は遠景ぼかしを切る） | CameraAttributes の遠景ぼかし（被写界深度）、フォグ／空気遠近、ボリュームフォグによる本物の光芒 | 設定＋シェーダー |
| 画質3段階（高: 解像度倍率2・AO・影4096／標準: 1・影1024〜2048／軽量: 0.75・影512・SMAA と深度ぼかしなし）。Intel／SwiftShader は「標準」から始める | `scaling_3d_scale`（FSR）と、Environment／Light を切り替えるスクリプト | スクリプト |
| 速度の工夫: 2ピクセル未満の物は描かない、動いた物だけ行列を更新、AO の前処理を120mに限定、ローカルライトに予算を設ける、シェーダーの事前ウォームアップ | 表示距離（visibility range）、オクルージョンカリング、クラスタ化ライティング（多数のライトに強い）、シェーダーキャッシュ | 既存機能 |
| 素材: HDRI、月の画像、流れる雲のシェーダー、スキャンした自然素材、建物の正面を並べた画像（地域テクスチャ約100MB）、PBR マテリアル | 素材の側の話。エンジン側は取り込みの仕組みがあれば足りる | 素材 |

## 結論

- IKEBEROTH の光と影は、ほぼすべて Godot の既存機能の「設定」と「後処理シェーダー」で再現できる。エンジンの改造は要らない。
- ACES、LUT、SMAA、段階分けした影、間接光（SDFGI／SSIL）、ボリュームフォグなど、必要な機能はこのツリーにあることをコードで確認した（`scene/resources/environment.h`、`camera_attributes.h`、`scene/main/viewport.h` の `SCREEN_SPACE_AA_SMAA`、`scene/3d/light_3d.h` の `SHADOW_PARALLEL_4_SPLITS`）。
- 差は「初期状態で全部オフ」と「調整と素材の積み上げ（IKEBEROTH は改訂373回）」から来ている。
- 互換性の注意: Environment などのクラスの初期値を変えると、初期値の項目を保存していない既存の .tscn／.tres の見た目が変わってしまう（プロジェクト形式の互換を壊す）。そのため、提供するのはプリセットのリソースかテンプレートにする。
