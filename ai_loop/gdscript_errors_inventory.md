# GDScript のエラー／警告の読みにくさ一覧（B-103, 2026-11-17）

エージェント（と作者）が「どこで起きたか」「どう直すか」を読み取れないものの一覧。
実際にエラーを起こすスクリプトを、`engine.sh run`（実行時）と `engine.sh check`（解析時）で走らせて集めた。
例はすべて実際の出力。

判定の目安:
- **位置**: 直すべきスクリプトの行が分かるか
- **直し方**: 何をすればよいかが文面から分かるか
- **漏れ**: 本当は問題があるのに出ないものはないか

## 一覧

| # | 種類 | 例（実際の出力） | 位置 | 直し方 | 漏れ | 状況 |
|---|---|---|---|---|---|---|
| 1 | エンジン側のエラーがスクリプトから呼ばれた（push_error、get_node、load など） | `push_error("...")` → `core/variant/variant_utility.cpp:1024`。`get_node("Nope")` → `scene/main/node.cpp:1993 Node not found: "Nope"`。`load("res://missing.tres")` → `resource_loader.cpp:317` | ✗ C++ の位置しか出ない | △ | — | **この版で対応**: `--log-json` に `script_file`／`script_line`／`script_function` を付けた |
| 2 | 実行時エラーで関数が止まる | 同じ `_ready` にある2つ目以降のエラー（`z.foo` など）は出ない | ○ | ○ | ✗ 1回の実行で1関数1件まで | 未対応。直して再実行するしかない。B-303（自己検証プレイ）で繰り返し実行する前提にする |
| 3 | 存在しないメソッドへの connect | `connect("ready", Callable(self, "no_such_method"))` は何も出ない。発火した時点で object.cpp のエラーになる | ✗ | ✗ | ✗ 接続した時点では出ない | 未対応。候補: validate（B-203）で、シーン内の接続先メソッドの有無を調べる |
| 4 | `$Missing`・`get_node("...")` の存在しないノード | 解析では出ない。実行時に #1 の形で出る | #1 の対応で ○ | ○ | 解析時に ✗ | 候補: validate でシーンとスクリプトを突き合わせる（スクリプトがどのシーンに付くかが分かる場合だけ） |
| 5 | 1つの問題でエラーが2件出る | `var hp: int = "full"` → `Cannot assign a value of type "String" as "int".` と `Cannot assign a value of type String to variable "hp" with specified type int.` が同じ行・列に出る | ○ | ○ | — | 未対応。エージェントは同じ行・列の重複をまとめて読めばよい。エンジン側でまとめるのは解析器の挙動の変更になるので、別課題 |
| 6 | 構文エラーの後に解析エラーが出ない | `if` の後のブロックがないと `Expected indented block after "if" block.` だけが出る。同じファイルの型エラーなどは、構文を直すまで出ない | ○ | ○ | △ 直すと次が出る | 仕様（構文が壊れると解析できない）。check を繰り返す運用で足りる |
| 7 | 構文エラーの連鎖 | `func _ready(:` → `Expected parameter name.`、`Expected closing ")" after function parameters.`、`Expected indented block after function declaration.`（同じ箇所が原因） | ○ | △ 最初の1件が原因 | — | 運用: 最初のエラーから直す。check の結果は行・列の順なので、先頭を見ればよい |
| 8 | インデントの混在 | `Used space character for indentation instead of tab as used before in the file.` | ○（行と列） | ○ | — | 問題なし |
| 9 | 存在しない基底クラス | `extends Nodee` → `Could not find base class "Nodee".` | ○ | △ 似た名前の候補が出ない | — | 候補: 「もしかして Node」を足す（エラーメッセージの変更なので、互換性を確認してから） |
| 10 | 存在しない preload | `Preload file "res://nope.gd" does not exist.` | ○ | ○ | — | 問題なし。validate（B-203）でも見つかる |
| 11 | 解放済みのオブジェクト | `Invalid access to property or key 'name' on a base object of type 'previously freed'.` | ○（使った行） | △ 解放した場所が分からない | — | 未対応。解放した位置を覚える仕組みが要るので重い。後回し |

## 選んだ最初の1件: #1（エンジン側のエラーにスクリプトの位置を付ける）

- 理由:
  - 最も頻繁に出る（push_error、get_node、load は普通のゲームコードで多用する）。
  - 今は位置がまったく分からない。
  - 直す範囲が小さい: B-201 のハンドラーの中で、スクリプトのバックトレースの一番内側を読むだけ。
- 結果: `{"file": "scene/main/node.cpp", "line": 1993, ..., "script_file": "res://main.gd", "script_line": 5, "script_function": "_ready"}`
- 制約: スクリプトのバックトレースはデバッグ版（エディター版とデバッグ版テンプレート）で取れる。リリース版では、`script_*` が付かないことがある。
