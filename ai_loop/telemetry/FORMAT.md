# プレイテレメトリの形式（B-301, 2026-11-17）

作者と AI が読む前提の JSON 形式。2つある。

- **イベントログ**: 1回のプレイで出る出来事を1行1件で並べたもの（JSON Lines）。
- **レポート**: 多数のログを集計したもの（JSON 1つ）。

## 個人を追跡しない（設計上の制約）

- ログに入れてよい識別子は `session` だけ。プレイ開始時に作る使い捨ての乱数で、端末・アカウント・名前とは結び付けない。次のプレイでは別の値になる。
- 入れないもの: ユーザー ID、端末 ID、IP アドレス、時刻（年月日・時刻）。
  - 時間は「プレイ開始からの経過」（frame と time）だけを持つ。
- レポートは既定でコミュニティ単位（渡されたログ全部）の集計で、セッションごとの行は出さない。
  - 出すのは「そこに何セッションが来たか」という件数だけ。

## イベントログ（JSON Lines）

1行に1件。共通の項目:

| 項目 | 型 | 意味 |
|---|---|---|
| `v` | int | 形式の版。今は 1 |
| `session` | string | 使い捨てのセッション ID（`s-` ＋ 16桁の16進数） |
| `type` | string | `start`／`checkpoint`／`death`／`stall`／`clear`／`quit` |
| `frame` | int | 物理フレーム番号（プレイ開始が 0）。同じ入力なら同じ値になる |
| `time` | float | プレイ開始からの秒数（ゲーム内時間） |
| `scene` | string | その時のシーン（res://...） |
| `cell` | [int, int, int] | 位置を格子（既定 4 単位）に丸めたもの。プレイヤーの位置が分かるときだけ |

種類ごとの項目:

| type | 追加の項目 | いつ出すか |
|---|---|---|
| `start` | `game`（project の名前）、`version`（任意） | プレイ開始時 |
| `checkpoint` | `id`（string） | チェックポイントに着いたとき（ゲームが呼ぶ） |
| `death` | `cause`（string、任意） | 死亡したとき（ゲームが呼ぶ） |
| `stall` | `seconds`（float） | プレイヤーが同じ格子に一定時間（既定 10 秒）とどまったとき。格子を出るまで同じ場所では再度出さない |
| `clear` | — | クリアしたとき（ゲームが呼ぶ） |
| `quit` | `fps`（float、平均）、`frames`（int） | 終了時。最後の1件 |

例:

```json
{"v": 1, "session": "s-3f9a0c1d2e4b5a67", "type": "start", "frame": 0, "time": 0.0, "scene": "res://main.tscn", "game": "sample"}
{"v": 1, "session": "s-3f9a0c1d2e4b5a67", "type": "death", "frame": 212, "time": 3.53, "scene": "res://main.tscn", "cell": [75, 0, 0], "cause": "pit"}
```

## レポート（JSON）

`ai_loop/telemetry/report.py <ログ...>` が作る。

| 項目 | 意味 |
|---|---|
| `v` | 形式の版（1） |
| `sessions` | 集計したセッション数 |
| `cleared` | クリアに到達したセッション数 |
| `clear_rate` | cleared ÷ sessions |
| `deaths` | 死亡の合計 |
| `stuck_points` | 詰まり（同じ場所での死亡・停滞の集中）。場所ごとに `scene`・`cell`・`deaths`・`stalls`・`sessions`（そこで詰まったセッション数）。死亡＋停滞が多い順。2件以上の場所だけ |
| `checkpoints` | チェックポイントごとの到達セッション数（`id`、`sessions`）。最初に着いたフレームの中央値（`median_frame`）付き |
| `drop_off` | 離脱点。クリアせずに終わったセッションが、最後に着いたチェックポイントごとに何件か（`last_checkpoint`、`sessions`）。チェックポイントに1つも着いていなければ `last_checkpoint` は `null` |
| `fps` | `quit` の fps の平均（`average`）と最小（`min`） |

詰まり・離脱点は、作者がそのまま「どこを直すか」の候補として読める順に並べる。

## 記録のしかた

- `ai_loop/telemetry/telemetry.gd` を autoload（名前 `UZUTelemetry`）として入れる。
- ゲームは次を呼ぶ:
  - `UZUTelemetry.checkpoint("cp1")`
  - `UZUTelemetry.death("pit")`
  - `UZUTelemetry.clear()`
- プレイヤーのノードをグループ `uzu_player` に入れると、`cell` と `stall` が自動で付く。
- 出力先: コマンドライン引数（`--` の後）の `--uzu-telemetry <file>` で指定する。指定がなければ `user://telemetry/<session>.jsonl`。
