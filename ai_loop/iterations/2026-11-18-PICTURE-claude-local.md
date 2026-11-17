# 2026-11-18 PICTURE（Claude Code ローカル、Windows・RTX 4050）

一枚絵の 3D 化（U.ZU Picture）、画像からの PBR 生成、外部推定マップの取り込みを GDScript で試作し、GPU で確かめた。提案は `ai_loop/COORDINATION.md` の Q13。

## 置いたもの
`ai_loop/prototypes/picture/`: `uzu_picture.gd`／`uzu_picture.gdshader`／`uzu_fog_sheet.gdshader`（本体）、`picture_tool.gd`（analyze・snap・adopt・validate・schema）、`picture.py`（入口・shot）、`pbr_tool.gd`／`pbr_preview.gd`（PBR 生成と確認）、`map_tool.gd`（import・estimate）、`picture.json`（仕様の例）、`estimators.example.json`、`README.md`。
絵（`art/`）と Poly Haven の素材は公開リポジトリなので入れていない。

## 確かめたこと
- イケベロスのタイトル絵で全効果を同時に動かし、エラー 0 件。
- 生成法線と実スキャンの角度誤差、map import の判定結果は Q13 と README に表で記録。
- 途中で直した不具合: 背景メッシュに前景が写り込む（pull-push で穴埋め）、遠い空が裂ける（裂く判定を z 比から disparity 差へ）、GPU でハッシュノイズがブロック状（FastNoiseLite のシームレス画像に置換）、ドリフトで絵の端が見える（画角に余白）、光源候補が葉に偏る（局所コントラストで採点＋ snap）。

## 残り
- C++ 化（Q13 の P-1〜P-3）はクラウド側の判断待ち。
- brick の法線流儀判定が「不明」、rock のずれ回復が 16px 止まり（格子を細かくすれば改善の見込み）。
