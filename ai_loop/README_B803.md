# B-803: AI からのシェーダー検査とフレーム計測

統合済みの B-803 のコンパイラーとフレームレコーダーを利用する。
editor バイナリを `bin/` にビルドするか、環境変数 `GODOT` にパスを指定する。

```sh
python3 ai_loop/uzu.py engine /path/to/project check --path res://water.gdshader
python3 ai_loop/bench.py run --project /path/to/project --frames 60 --repeat 3 --out baseline.json
python3 ai_loop/bench.py run --project /path/to/project --frames 60 --render
```

`check` は `.gd`、`.gdshader`、`.gdshaderinc` を検査する。シェーダーは描画不要の
ShaderLanguage コンパイラーを使い、`errors` / `warnings` の各項目に
`file`・`line`・`column`・`code`・`message` を返す。行は1始まり、取得できない列は `-1`。
GPU ドライバー固有のコンパイル検証は含まない。

MCP では既存の `script_check` にシェーダーのパスを渡せる。
新しい `frame_benchmark` は既存 `bench.py run` を呼び、同じ JSON を返す。

```json
{"name":"script_check","arguments":{"path":"res://water.gdshader"}}
{"name":"frame_benchmark","arguments":{"frames":"60","repeat":"3","render":false}}
```

`frames` / `repeat` は既存 MCP ツールに合わせ文字列、`render` は真偽値。
`repeat` は正の整数。`frames` はウォームアップ10フレームの後に計測できるよう
12以上とする。計測用コピーで実行し、最初の1回を捨て、その後の各 mark の中央値を返す。

`marks` は既存の起動時の項目に加え、`[Frame] work average`・`p95`・`worst` を
**秒**で返す。work は物理・処理開始から最後の `_process` までの時間。
既定のヘッドレス実行では CPU の作業時間を測り、描画性能は測れない。
`--render` / `render: true` は既存の描画環境を利用し、描画・待ち時間を含む
`[Frame] wall average`・`p95`・`worst` も返す。環境と FPS 制限を揃えて比較する。

エンジンの異常終了、早期終了によるサンプル欠落、引数エラーは失敗とし、
CLI は `ok: false` の JSON と終了コード1、MCP は `isError: true` を返す。
起動計測だけ存在しても、フレームサンプルがなければ成功にしない。

```sh
python3 ai_loop/mcp_server.py --self-test
python3 ai_loop/bench.py --self-test
python3 ai_loop/uzu.py selftest engine
SCONS=/path/to/scons ai_loop/agent_finish.sh
```
