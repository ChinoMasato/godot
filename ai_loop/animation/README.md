# リグ付きモデルのモーションと連番スプライト

## 待機・歩行・走行

Blender ブリッジなどで作ったリグ付き GLB をプロジェクトに入れ、Godot にインポートします。次のコマンドで `MotionPlayer` と3本のループを持つ新しいシーンを作ります。

```sh
python3 ai_loop/uzu.py anim ./my_game motion --model res://scout.glb --out res://scout_motion.tscn
```

元のモデルは生成シーンから参照されます。そのシーンをプレイヤーの子として配置し、停止中は `MotionPlayer.play("idle")`、移動速度に応じて `walk` または `run` を再生します。クリップはその場で再生され、ゲーム内の移動は `CharacterBody3D` などが担当します。地面への足合わせやモーションのブレンドはゲーム側で設定します。

既定のボーン名は [Blender ブリッジのロボット見本](../blender/README.md#リグとアニメーション付きの見本)に合います。別のリグには `--bones bones.json` で、`root`、`left_thigh`、`right_thigh`、`left_shin`、`right_shin`、`left_arm`、`right_arm` の名前を個別に上書きできます。スケルトンの X が左右、Y が上の二足歩行モデルを想定しています。

| オプション | 調整対象 |
| --- | --- |
| `--speed 0.25–4` | 歩行・走行の再生速度。既定1 |
| `--stride 0.25–2` | 歩行・走行時の手足の振り幅。既定1 |
| `--weight 0–1` | 値が大きいほど上下動が控えめ。既定0.5 |

JSON は各クリップの秒数、始点と終点の回転差（度）と位置差（m）を返します。MCP の入口は `animation_motion` です。

## PNG 連番の位置合わせ

```sh
python3 ai_loop/uzu.py anim ./my_game align --frames-dir res://frames --out res://frames_aligned
```

透明余白を除いた各フレームを、全フレームを収める共通サイズへピクセル等倍で配置します。足元に相当する下端と横方向の中央をそろえます。左右の余白の差は整数ピクセルに丸めます。フレーム名は維持するため、順番は `01.png` のように揃えます。連番の不透明部分のサイズと配置座標を JSON で確認できます。MCP の入口は `animation_align` です。

入力と出力はプロジェクト内のパスです。出力には既存のファイルやディレクトリと重複しない名前を指定してください。
