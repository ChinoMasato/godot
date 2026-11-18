# 灯りの欠片（Shards of Light）— U.ZU Engine サンプルゲーム

U.ZU Engine の全機能を1本で実証する5分級の HD-2D ミニゲーム（Phase 10 / B-1001）。
夕方の村で「灯りの欠片」を3つ集め、祠のランタンに戻す。欠片の収集に応じて
昼→夕→夜へ光が変わる。

## 使い方
- タイトルで E / Space： 開始
- 矢印 / WASD： 移動、E： 話す・拾う・灯す

## 使っているエンジン機能
- HD-2D ルック一式（UZULook.apply_hd2d、Lit スプライト、BlobShadow、風揺れ素材、粒子）
- U.ZU Picture（タイトル画面の一枚絵パララックス、title/picture.json）
- テレメトリ（UZUTelemetry autoload、checkpoint / clear を記録）
- 開発は CLI のみ（エディタ GUI を使わない）

## 素材について
- キャラのドット絵は ai_loop/look_samples/hd2d/hero.png の色替え（自作）。
- タイトル絵は PIL で描いた自作イラスト＋深度マップ。
- game_font.ttf は Noto Sans CJK のサブセット（OFL 1.1、art/FONT_LICENSE.txt）。
