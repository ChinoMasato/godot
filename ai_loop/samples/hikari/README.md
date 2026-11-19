# 灯りの欠片（Shards of Light）— U.ZU Engine サンプルゲーム

U.ZU Engine の全機能を1本で実証する5分級の HD-2D ミニゲーム（Phase 10 / B-1001）。
夕方の村で「灯りの欠片」を3つ集め、祠のランタンに戻す。欠片の収集に応じて
昼→夕→夜へ光が変わる。

## 使い方
- タイトルで E / Space / タップ： 開始
- 矢印 / WASD： 移動、E： 話す・拾う・灯す
- タッチ操作（B-1004）： タッチスクリーン検出時に左下へ仮想スティック（移動）、
  右下へインタラクトボタン（ラベルは近くの対象に連動）を表示する。
  デスクトップでの強制表示は `--uzu-touch-ui` 引数か環境変数 `UZU_TOUCH_UI=1`

## UI（B-1306）
- HUD・プロンプト・会話・クリア画面は UZUUI＋世界観テーマ「wafu」で構築し、
  開閉と収集カウントには UZUUIJuice の動きを使う
- タイトル画面のオーバーレイは宣言的 UI 構築（B-1301）の生成シーン
  `ui/title_ui.tscn`。再生成は
  `python3 ../../uzu.py ui . --spec ui/title_ui.json`（このフォルダで実行）

## 使っているエンジン機能
- HD-2D ルック一式（UZULook.apply_hd2d、Lit スプライト、BlobShadow、風揺れ素材、粒子）
- U.ZU Picture（タイトル画面の一枚絵パララックス、title/picture.json）
- テレメトリ（UZUTelemetry autoload、checkpoint / clear を記録）
- 開発は CLI のみ（エディタ GUI を使わない）

## 素材について
- キャラのドット絵は ai_loop/look_samples/hd2d/hero.png の色替え（自作）。
- タイトル絵は PIL で描いた自作イラスト＋深度マップ。
- game_font.ttf は Noto Sans CJK のサブセット（OFL 1.1、art/FONT_LICENSE.txt）。
