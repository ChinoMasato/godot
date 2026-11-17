# U.ZU ルックを AI エージェントから使えるようにする（UZULook API と look.sh） — claude (2026-11-17)

- ブランチ: claude/uzu-engine-briefing-rrno7b
- オーナーの指示（2026-11-17）: 「基本は AI エージェントを通して使えるようにしてください」。B-704（素材）・B-705（補助光・街灯）・B-712（カメラ）の「エディタの UI をどうするか」という確認への回答。エディタ UI ではなく、エージェントから使える形を基本にする。
- 公開 API を増やす変更（新しいクラス `UZULook`）。オーナーの指示による。

## 変更概要

1. **`UZULook` クラス**（`scene/3d/uzu_look.{h,cpp}`、`scene/register_scene_types.cpp` で登録。3D を無効にしたビルドでは入らない）
   - すべて static。GDScript／C# から `UZULook.create_environment(...)` のように呼べる。
   - LookPreset3D の関数をそのまま公開: `create_environment`、`create_camera_attributes`、`configure_sun`、`configure_fill_light`、`configure_rim_light`、`configure_lamp`、`create_material`、`configure_camera`、`get_lens_fov`、`get_eye_height`、`get_default_quality`。
   - エージェント向けのシーン操作（新しいノードは root の持ち物にするので、そのまま保存される）:
     - `apply_to_scene(root, quality, atmosphere)`: WorldEnvironment を追加（あれば更新）し、空を照らす DirectionalLight3D が無ければ太陽を追加する。何をしたかを Dictionary で返す。
     - `add_lighting_rig(root, atmosphere)`: 補助光と背面光を追加。
     - `add_lamp(root, position, quality)`: 街灯（OmniLight3D と光る電球）を追加。
   - 列挙: Quality／Atmosphere／MaterialKind／Lens／EyeLevel。
   - クラスリファレンス `doc/classes/UZULook.xml`: `--doctool` で生成し、説明と GDScript／C# の例を書いた。`make_rst.py --dry-run` のエラー件数は追加前と同じ（283。既存のもの）。
2. **エージェント用のコマンド** `ai_loop/look.sh <project> <command> ...`（中身は `ai_loop/look_tool.gd`。エディタ版のバイナリを `--headless --script` で動かす）
   - `apply`／`rig`／`lamp`／`camera`（レンズ・目の高さ・シフト）／`material`（.tres に保存）／`info`。
   - 結果は JSON を1行で返す（`{"ok": true, ...}`）。失敗したら `ok: false` とエラー文、終了コード 1。
3. **ヘッドレスで保存すると LUT が消える不具合の修正**（B-703 で見つけた制約）
   - 原因: ダミーのレンダラー（`servers/rendering/dummy/storage/texture_storage.h`）は 2D テクスチャのデータは保持するが、3D テクスチャのデータは捨てていた。そのため `ImageTexture3D` を含むシーンをヘッドレスで保存すると、中身が空になった。
   - 修正: 2D と同じように、3D テクスチャのスライスも複製して保持し、`texture_3d_get` で返す。
   - B-703 で外していた「LUT のテクセルの値」のテストを戻した（このテストがヘッドレスでの読み出しも確かめる）。

## テスト

- `tests/scene/test_uzu_look.cpp`（新規、2件）:
  - クラスが登録され、static メソッドが ClassDB から呼べること（`get_lens_fov(LENS_TELEPHOTO)` = 25）。列挙が定数として見えること。
  - `apply_to_scene`: 1回目は WorldEnvironment と Sun を追加し、どちらも root の持ち物。2回目は更新と維持で、ノードは増えない。補助光・背面光は太陽として数えない。街灯の位置、電球が発光すること、持ち物になっていること。
- 全テスト（エディタ版＋テスト）: **1474件中 1474 pass**／0 fail。ビルドの警告なし。clang-format 18: pass。

## エージェントのコマンドでの確認（ヘッドレス）

- テスト用プロジェクトで `info` → `apply --atmosphere humid` → `rig` → `lamp --position 2,3,-4` → `camera --lens standard --eye standing --shift 0.1` → `material --kind water --out res://materials/water.tres` を順に実行し、すべて `ok: true`。
- 保存したシーンには、WorldEnvironment・PhysicalSky・ImageTexture3D（LUT のデータつき）・Sun・FillLight・RimLight・Lamp/Bulb が入り、カメラは frustum 投影・FOV 45°・高さ 1.6m になった。
- 間違った入力（無いシーン、知らない空気の名前）では `ok: false`、終了コード 1。
- できたシーンを A-1 で描画し（比較用に値を手で写す必要がなくなった）、`palette_check.py` で測った。前は明暗差 0.14、後は 0.21。どちらも「明暗差が弱い」のヒントが出た（湿った空気で、色の無いマテリアルのシーンのため）。
