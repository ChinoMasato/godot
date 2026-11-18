# 自然マップと都市マップの自動生成（UZUWorldGen） — Claude Code ローカル（Windows、RTX 4050、2026-11-19）

オーナーの依頼: 「自然マップや都市マップの自動生成機能はエンジン内に組み込んで欲しい」「AI が依頼しやすい形で組み込んで」。
BACKLOG の番号はまだない（登録はリードに頼む）。ブランチは `agent/worldgen-claude-local`。

クラウドが作業中のファイル（scene/、editor/、build_tool.gd（B-1208））には触れていない。
エンジン側は独立したモジュール `modules/uzu_worldgen/` にまとめた（自前で登録するので、共有ファイルの変更はない）。

## AI が依頼しやすくするために決めたこと

1. **キーは全部省略できる。** プリセット（自然: plains／hills／mountains／island／valley／lakes、都市: village／town／city）が残りを埋め、どのキーでも上書きできる。最短の依頼は `{"preset": "island"}`。
2. **同じ依頼なら同じ地図になる**（seed）。作り直しや比較がしやすい。
3. **間違いは何も作らずに、直し方を返す。**
   - 知らないキー: 一番近いキー（例: `tress` → 「"trees" のことでは？」）と、キーの一覧を返す。
   - 型の違い・範囲外: 受け付ける値の範囲を返す。
   - キー同士の矛盾: 直す値を返す（例: size/cell が多すぎるとき → cell をいくつ以上にするか）。
4. **次に使う場所を名前つきで返す。**
   - 自然: spawn（中央付近の平らで乾いた所）、peak、water、shore（岸の平らな所）、clearings（木の少ない平地 最大3）。
   - 都市: spawn（中央の交差点）、plaza、gate_north／south／east／west、tallest_roof。
   - プレイヤーや家・村をすぐ置ける。
5. **数で結果を返す。** 木・岩・草・建物・窓・街灯の数、三角形数、高さの範囲、水の割合、生成にかかった時間。上限で止めたときはヒントを返す（例: 街灯 60 のうち光るのは 30（max_lights））。
6. **上から見た 256 px の地図を返す**（`--overview`）。画像が読める AI なら、配置が一目でわかる。
7. **一覧がある。** `uzu.py map options` で、すべてのキーの既定値・受け付ける値・意味と、プリセットの中身を返す（プロジェクト不要）。

## 中身

- **エンジン（C++）** `modules/uzu_worldgen/`（UZUWorldGen、静的 API）:
  - `generate_nature(root, spec)`、`generate_city(root, spec)`、`resolve(kind, spec)`、`get_options(kind)`、`get_presets(kind)`、`height_at(nature, x, z)`。
  - 自然:
    - 地形: 勾配ノイズ（quintic で格子が見えない）の多層和に、尾根を混ぜる。形は open／island（周りが海）／valley（蛇行する川）。
    - 色: 高さ・傾き・水際で、草・土・岩・砂・雪を頂点色で塗る。
    - 水: B-727 の水を使う（島の海は地図の 3 倍に広げて、端を見せない）。
    - 植生: 森の濃淡ノイズで林と空き地を作る。水際より上が高い所ほど針葉樹にする。岩は斜面に多くする。草は平らな乾いた所。
    - どれも MultiMesh で描く。当たり判定は HeightMapShape3D（等倍スケール）。
  - 都市:
    - 道: 交差点の格子を作る。organic では交差点をずらし、曲がった道と不揃いな街区にする。
    - 街区: 縁石 15 cm の歩道を作る。敷地は不揃いな幅に分け、深い街区は 2 列にする。
    - 建物: 階数は中心ほど高い。屋根は切妻か平ら。各階に窓（一部が光る）。
    - 広場と噴水、街灯（光るのは max_lights 個まで）、街路樹。当たり判定（地面・歩道・建物）も付ける。
    - 色の組み: warm／stone／modern／pastel。
  - `height_at` は、地形の三角形の上で正確な高さを返す（テストで頂点と一致を確認）。
- **ツール**:
  - `ai_loop/map_tool.gd`（新規）。
  - `uzu.py map <project> nature|city --scene ... [--spec JSON か ファイル] [--set key=value ...] [--overview] [--replace] [--camera] [--look golden_hour] [--shot]`、`uzu.py map options`。
  - MCP: map_nature、map_city、map_options。
  - 大きなデータ（メッシュ・小物の配置・高さマップ）は、シーンの横の `res://<scene>_maps/<name>/*.res`（バイナリ）に保存する。.tscn は小さいまま、AI が読める（`--inline` で中に入れることもできる）。
- **ドキュメント**: `modules/uzu_worldgen/doc_classes/UZUWorldGen.xml`。

## 確認

- 単体テスト（`[SceneTree][UZUWorldGen]` 5件、139 アサーション）: pass。中身は次のとおり。
  - 間違いの返し方: 近いキー、範囲、型、キー同士の矛盾。誤りのときは何も追加しない。
  - プリセットと上書き。
  - 同じ seed で同じ頂点・同じ数、違う seed で違う地図。
  - height_at が頂点と一致する。spawn が水より上。新しいノードはすべてシーンに保存される（owner）。
  - 島は端が海で、水の割合が 0.2〜0.9。
  - 都市: 同じ seed で同じ。格子の道の上に建物の角がない。歩道の上に建つ。光る街灯が max_lights で止まり、ヒントが出る。
- `uzu.py selftest engine`: ok（map の 4 項目を追加: 島、村、同名で --replace なしは断る、打ち間違い）。`mcp_server.py --self-test`: ok。
- 全テスト（Windows）: 1522 件 pass、失敗 0、スキップ 3（ai-native 24a2621591 ＋このモジュール）。
- GPU（RTX 4050、`--look golden_hour --shot`）: 画像は `ai_loop/look_samples/compare/worldgen_windows.png`（左: 撮影、右: 上からの地図）。

| 依頼 | 生成時間 | 数 |
|---|---|---|
| `{"preset": "island", "seed": 7}`（256 m） | 456 ms | 木 243（広葉 168、針葉 75）、岩 62、水 68% |
| `{"preset": "valley", "seed": 4}`（256 m） | 657 ms | 木 182、岩 192 |
| `{"preset": "town", "seed": 2}`（200 m） | 23 ms | 建物 126、窓 3672（光る 1247）、街灯 60（光る 30）、街路樹 202 |
| `{"preset": "village", "seed": 5}`（120 m、organic） | 7 ms | 建物 59、窓 587、街灯 43、街路樹 104 |
| `{"preset": "city", "seed": 3}`（320 m、夜） | 39 ms | 建物 134、窓 28992、三角形 64278 |

## 限り（次の段）

- 都市は平らな地面だけ。自然の地図の上に村を置く（clearing を平らにして都市を載せる）のは次の段。
- 道の網は格子が元。放射状の道や川沿いの道はまだない。
- 建物は箱と屋根と窓の組み合わせ。入口、ベランダ、看板、内装はない。PBR テクスチャ（B-902／B-1209）とつなぐと良くなる。
- 歩ける範囲（NavigationMesh）はまだ出していない。
- `uzu.py build` の仕様（B-1207）に `"nature"`／`"city"` の段を足すのは、build_tool.gd を触っているクラウド側で行うのがよい（UZUWorldGen.generate_* を呼ぶだけ）。
- clang-format はこの PC に無いので、手で Godot の書式に合わせた（CI の確認に任せる）。
- UZULook.add_lamp は、電球の owner を p_root にしている。p_root がシーンの根でないと、電球が保存されない（今回は使わず、configure_lamp を使った）。クラウド担当のコードなので報告のみ。
