# Blender ブリッジ（B-1201）

外部の Blender をヘッドレスで起動し、Python で作ったモデルの GLB とプレビュー PNG を Godot プロジェクトに配置する。Python 3.9 以降と、glTF エクスポーターが利用できる Blender が必要。Blender 4.3.2 で検証した。

```sh
python3 ai_loop/blender_bridge.py \
  --project /path/to/godot-project \
  --script ai_loop/blender/crate.py \
  --out props/supply_crate.glb
```

`--project` は `project.godot` のあるディレクトリ、`--out` はその中の相対パス。見本では `props/supply_crate.glb` と `props/supply_crate.preview.png` ができる。成功時は `ok`、両方の `res://` パス、ファイルサイズを JSON で1行返す。Godot で通常通り取り込むか、ヘッドレスで `godot --headless --path /path/to/godot-project --import` を実行する。

Blender が PATH にない場合は `--blender` または環境変数 `BLENDER` に実行ファイルを指定する。処理時間は `--timeout`（既定300秒）、スレッド数は `--threads`（既定2）で指定できる。

素材スクリプトは信頼できる Python ファイルを渡す。空のシーンにメッシュを作り、`bpy.context.scene.camera`、照明、レンダー設定を指定する。ブリッジは現在のシーンの PNG を撮り、カメラとライトを除いた GLB に書き出す。モディファイアは GLB のメッシュに反映する。見本は外部素材不要で、Cycles の CPU レンダーを使う。

PNG と GLB が生成されてからプロジェクトへ配置する。既存のファイルは保護し、出力済みの名前への再実行は失敗する。別の `--out` を指定して比較できる。配置先はハードリンクをサポートするファイルシステムが必要。

## リグとアニメーション付きの見本

`scout.py` は小型の配達ロボットを作る。11本のボーンにメッシュの部位を割り当て、`IdleLoop`（待機、約2秒）と `WalkLoop`（その場歩行、約1秒）を GLB に書き出す。Godot では `Skeleton3D`、スキン付きメッシュ、`AnimationPlayer` として読み込める。シーン内の移動はゲーム側で与え、移動中に `WalkLoop`、停止中に `IdleLoop` を再生する。

```sh
python3 ai_loop/blender_bridge.py \
  --project /path/to/test-project \
  --script ai_loop/blender/scout.py \
  --out scout.glb
godot --headless --path /path/to/test-project --import
godot --headless --path /path/to/test-project --script "$PWD/ai_loop/blender/check_scout.gd"
```

最後のコマンドは11本のボーンとスキン、両クリップのループ設定、再生位置によるボーンの回転、始点と終点のポーズを検証する。
