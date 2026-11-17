# 橋 API の契約（案 v0、B-503, 2026-11-17）— オーナー承認待ち

U.ZU Engine（制作側）と Gogashare（プレイ側）の境界の API。`ai_loop/BRIEFING.md` の「4. 製品構成」（分離）と「5. 場所の思想」にもとづく。

**この文書は案。** API の形は設計の判断なので、オーナーの承認があるまで実装しない（COORDINATION の Q9）。

## 決まり（分離の原則から）

- エンジン側が持つのは「橋」の3つだけ。
  1. 公開（書き出したビルドのアップロードとメタ情報）
  2. 戻ってきた感想・テレメトリを読む
  3. 試遊会への参加口
- コミュニティ機能の本体（ルーム、発見、観戦、応援）は Gogashare 側にあり、この API には出さない。
- **点数・順位・売上を返す項目は持たない。** 作者に返す主役の数字は「遊んでくれた人数」と「届いた感想の数」だけ。
- 感想は B-601 の形式で返す。本文（`message`）は書き換えずに返す。
- テレメトリは B-301 の**レポート**（集計済み）だけを返す。プレイごとの生のログは Gogashare から出さない（個人を追跡しない）。
- 認証は作者のトークン（`Authorization: Bearer`）。トークンはプロジェクトのファイルに書かない（環境変数か、資格情報の保管場所）。
- 版は URL に入れる（`/v1/...`）。項目を足すのは互換、消す・意味を変えるのは新しい版。

## 一覧

| # | 区分 | メソッドとパス | 用途 |
|---|---|---|---|
| 1 | 公開 | `POST /v1/games` | ゲームを登録する（初回だけ） |
| 2 | 公開 | `POST /v1/games/{game_id}/builds` | ビルドの登録を始め、アップロード先を受け取る |
| 3 | 公開 | `PUT {upload_url}` | ビルド（zip）を送る |
| 4 | 公開 | `POST /v1/builds/{build_id}/finalize` | 送り終えたことを伝え、遊べる URL を受け取る |
| 5 | 公開 | `GET /v1/games/{game_id}/builds` | 公開したビルドの一覧 |
| 6 | 感想 | `GET /v1/games/{game_id}/feedback?since={cursor}` | 届いた感想（B-601 の形式） |
| 7 | テレメトリ | `GET /v1/games/{game_id}/telemetry/report?build={build_id}` | 集計済みのレポート（B-301 の形式） |
| 8 | 感想・テレメトリ | `GET /v1/games/{game_id}/summary` | 主役の数字（遊んでくれた人数、届いた感想の数） |
| 9 | 試遊会 | `GET /v1/games/{game_id}/sessions` | このゲームの試遊会の予定 |
| 10 | 試遊会 | `POST /v1/sessions/{session_id}/join` | 作者として試遊会に入る URL を受け取る |
| 11 | 試遊会 | `GET /v1/sessions/{session_id}/events?since={cursor}` | 試遊会の出来事（人が来た、感想が届いた） |

## JSON 形式

### 1. ゲームの登録

```json
// 要求
{"title": "穴とびランナー", "description": "穴を飛び越える一本道。", "room": "class-2026-a"}
// 応答 201
{"game_id": "g_7f3a", "page_url": "https://gogashare.example/g/g_7f3a"}
```

`room` は任意（クラス／ジャム／チームの「渦」）。

### 2〜4. ビルドの公開

```json
// 2. 要求
{"version": "0.3", "platform": "web", "prototype": true, "notes": "2つ目の穴を広げた",
 "size": 18234567, "sha256": "…", "prepublish": {"ok": true, "size_mb": 17.4, "files": 12, "problems": []}}
// 2. 応答 201
{"build_id": "b_19c2", "upload_url": "https://…", "upload_expires_in": 900}
// 4. 応答 200
{"build_id": "b_19c2", "status": "live", "play_url": "https://gogashare.example/play/b_19c2"}
```

- `prototype` の既定は true（作りかけを出すのが標準）。
- `prepublish` は B-502（`prepublish.py check`）の結果をそのまま入れる。`ok` が false なら、2 の要求は `422 prepublish_failed` になる。
- `platform` は今は `web` だけ。

### 5. ビルドの一覧

```json
{"builds": [{"build_id": "b_19c2", "version": "0.3", "status": "live", "play_url": "…", "published": "2026-11-17"}]}
```

### 6. 感想

```json
{"items": [
  {"id": "f_0012", "build_id": "b_19c2", "v": 1, "kind": "stuck", "message": "2つ目の穴、何回やっても落ちる…",
   "where": {"scene": "res://main.tscn", "checkpoint": "cp2"}}
], "next": "c_88"}
```

- 各項目は B-601 の1件の形式に、`id` と `build_id` を足したもの。
- `signature` は、プレイヤーが書いたときだけ入る。
- `next` を次の `since` に使う。エンジン側は `ai_loop/feedback/to_backlog.py` で BACKLOG の課題と「贈り物」のファイルにする。

### 7. テレメトリのレポート

応答は `ai_loop/telemetry/FORMAT.md` のレポートそのもの（`v`・`sessions`・`cleared`・`clear_rate`・`deaths`・`stuck_points`・`checkpoints`・`drop_off`・`fps`）。

- Gogashare が集計してから返す。
- セッション数が少ない（5 未満）ときは、`stuck_points` と `drop_off` を空にする。少人数だと個人のプレイが分かってしまうため。

### 8. 主役の数字

```json
{"players": 23, "feedback": 7, "first_player_waiting": false}
```

- `first_player_waiting`: まだ誰にも遊ばれていない（最初の1人を待っている）。
- 順位・点数・売上の項目は持たない（追加もしない）。

### 9〜11. 試遊会

```json
// 9. 応答
{"sessions": [{"session_id": "s_41", "room": "class-2026-a", "starts": "2026-11-20T15:00+09:00", "status": "scheduled"}]}
// 10. 応答
{"join_url": "https://gogashare.example/live/s_41?as=author", "role": "author"}
// 11. 応答
{"events": [{"type": "player_joined", "count": 4}, {"type": "feedback", "id": "f_0013"}], "next": "c_9"}
```

- 11 の出来事に個人の名前は入れない（人数と感想の ID だけ）。

## 誤りの形式（全エンドポイント共通）

```json
{"error": {"code": "prepublish_failed", "message": "the engine's MIT license is not in the build"}}
```

| HTTP | code | 意味 |
|---|---|---|
| 400 | `bad_request` | 形式の誤り |
| 401 | `unauthorized` | トークンがない・無効 |
| 404 | `not_found` | ゲーム・ビルド・試遊会がない |
| 413 | `too_large` | ビルドが大きすぎる |
| 422 | `prepublish_failed` | 公開前チェックが通っていない |
| 429 | `rate_limited` | 呼び出しが多すぎる |

## エンジン側の対応（実装は承認の後）

| API | エンジン側のコマンド（予定） | 既にある部品 |
|---|---|---|
| 1〜5 | `ai_loop/publish.sh <project>`（書き出し → prepublish → 2〜4） | B-502 `prepublish.py` |
| 6 | `ai_loop/feedback/pull.sh` → `to_backlog.py` | B-601 |
| 7・8 | `ai_loop/telemetry/pull.sh` | B-301 `report.py`（同じ形式なので、そのまま読める） |
| 9〜11 | `ai_loop/live.sh` | なし |

## 承認してほしいこと（Q9）

1. 一覧の11本で足りるか（多すぎないか）。
2. テレメトリを集計済みのものだけにすること。少人数のときは、詰まりと離脱点を隠すこと（しきい値は 5 セッション）。
3. ビルドの公開で、公開前チェックの通過を必須にすること（422）。
4. 実装の順番: 公開（1〜5）→ 感想（6）→ テレメトリ（7・8）→ 試遊会（9〜11）。Gogashare 側ができるまでは、エンジン側をモックサーバーで試す。
