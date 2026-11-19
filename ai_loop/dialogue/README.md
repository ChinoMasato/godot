# 会話とクエストフラグ

[灯りの欠片の見本](lantern_quest.json)では、欠片を拾う経路と広場へ直行する経路が同じ灯籠のノードに合流します。灯籠で選べる行動は、欠片を持っているかどうかで変わります。

```sh
python3 ai_loop/uzu.py dialogue --spec ai_loop/dialogue/lantern_quest.json
python3 ai_loop/uzu.py dialogue --spec ai_loop/dialogue/lantern_quest.json --out /tmp/lantern_graph.json --max-chars 80
```

MCP からは `dialogue_validate` に `{"spec":"ai_loop/dialogue/lantern_quest.json"}` を渡します。相対パスの起点は CLI と MCP インスタンスの作業ディレクトリです。任意の引数 `out` と `max-chars` も文字列で渡せます。

## データ形式

| フィールド | 意味 |
| --- | --- |
| `entry` | 最初に表示するノードの ID |
| `flags` | フラグ名と初期値。省略時は空。値の `null` は未設定を表す |
| `nodes` | ノード ID をキーとする会話データ。各ノードに `text` が必要で、`speaker` も指定できる |
| ノードの `set` | ノードに入った時点で適用するフラグの更新 |
| ノードの `choices` | 選択肢の配列。各項目に表示用の `text` と遷移先 ID の `to` が必要 |
| 選択肢の `when` | フラグがすべて指定値に一致すれば選択可能。省略時は常に選択可能 |
| ノードの `end` | `true` で会話が終了。このノードは選択肢を持たない |

`set` と `when` は、宣言したフラグに `true` または `false` を対応させるオブジェクトです。

## 検証結果

CLI と MCP は `issues`（診断コード、JSON 内の位置、詳細）、`reachable_nodes`、`states_checked`、`text_lengths` を含む JSON を返します。到達可能なフラグの組み合わせごとに会話をたどります。分岐漏れや未設定フラグを報告するときは、その時点のフラグ値も返します。到達状態の上限は 16,384 です。

文字数は改行を除く Unicode コードポイントで数え、既定の上限は台詞、話者名、選択肢それぞれ 160 文字です。結果をゲームに取り込むときは、検証に合格した JSON を `--out` で書き出せます。CLI の終了コードは合格が 0、診断ありが 1、引数・入出力・JSON 構文のエラーが 2 です。
