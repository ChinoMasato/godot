# 『灯りの欠片』実プレイ不具合修正（2026-11-20）

担当: Muse (subagent) / ブランチ: agent/hikari-fix-20261120 / worktree: ~/workspace/godot-hikari-fix
起点: ai-native 7b1f64dd4ee

オーナーの Windows 実プレイ＋ログで確認済みの3件を直す。

## 問題1UZUPicture が書き出し版で画像を読めない
- 症状: exe で `ERROR: Error opening file 'title/art.png'`、レリーフが作られず平絵のみ。
- 原因箇所: scene/3d/uzu_picture.cpp `UZUPicture::load_image` が
`Image::load_from_file(ProjectSettings::globalize_path(p_path))`。
res:// の globalize は書き出し版では pck 外の実パスになり届かない。
- 方針: ResourceLoader（Texture2D→get_image()）を優先、失敗時のみ従来フォールバック。
user://・絶対パスの従来動作は壊さない。
- テスト: 既存テスト確認＋res:// 経由の読みをカバーするテスト追加。全テスト pass（既知 [UDPServer] 2件除く）。

## 問題2 会話 UI が左上に HUD と重なる
- 症状: 1173x660 ウィンドウで会話ウィンドウ（UZUUI.make_talk_window、PRESET_BOTTOM_WIDE＋オフセット）が
画面下ではなく左上に小さく描かれ HUD と重なり見切れる。
- 再現: capture_ui.py 等で同程度のサイズで会話表示を撮影。再現できなければ条件とともに正直に記録。
- 修正: 証拠に基づいて原因特定（アンカー／オフセット設定順、add_child タイミング、UZUUI 生成物構造）。

## 問題3 タイトルの開始が分かりにくい・読み込み中が無反応に見える
- 修正: (a) 「クリックではじめる」等の明確な開始プロンプト（静かな動きつき）。正は ui/title_ui.json。
(b) 開始入力→即「読み込み中…」表示、main.tscn は load_threaded_request で非同期読み込み後に切替。

## 検証
- verify_playthrough.py pass（タイトル開始経路の変更後もドライバ開始操作が効くこと）
- エンジン全テスト pass（既知2件除く）
- 問題2は修正前後の撮影を比較できる形で残す

## 進捗
- 2026-11-20 開始。worktree・ブランチ・CLAIMS 宣言済み。
- 問題1: `UZUPicture::load_image` を ResourceLoader 優先＋ファイルフォールバックに修正。
  テスト追加（PCK をマウントして res:// から読む＋build するケース）。ビルド・テストは後段。
- 問題3: title.gd に開始プロンプトの点滅（Hint プラークの modulate パルス）、
  「読み込み中…」ラベル即時表示、main.tscn の load_threaded_request 化を実装。
  title_ui.json のヒント文言を「クリック・タップ・Eキー： はじめる」に変更し tscn 再生成済み。
- 問題2: オーナーのスクリーンショット（image-f9da27e5.png、1149x677）を拡大して幾何を実測。
  - 左上で壊れているパネルは会話ウィンドウではなく **クリアパネル** と特定。
    根拠: 表示テキスト「ありがとう。」はクリア文の最終行「遊んでくれてありがとう。」の末尾、
    HUD の目的文も「村に灯りが戻った！」（末尾「た！」のみ可視）、欠片 3/3・夜景・点灯済みランタン。
  - 会話ウィンドウ自体は capture_ui.py（1152x648）で再現撮影し、画面下に正しく出ることを確認
    （before_dialogue.png）。PRESET_BOTTOM_WIDE＋オフセット指定は正しく機能している。
  - クリアパネルの原因: main.gd の `clear_panel.position = -clear_panel.size / 2.0`。
    Control.position は親空間座標なので、PRESET_CENTER のつもりでも左上隅に
    (-w/2, -h/2) で置かれ、半分が画面外に出る。実測した可視範囲（右端 x≈163、
    下端 y≈117）とも一致（パネル幅 ≈330 → -165..163）。
  - 修正方針: position ではなくオフセットで中心合わせする
    （offset_left/top = -size/2、offset_right/bottom = +size/2）。
  - クリア画面の再現撮影（capture の変種ドライバで shards=3 → _touch_lantern）:
    **再現成功**（before_clear.png）。オーナーのスクリーンショットと同一の構図で
    クリアパネルが左上に半分はみ出して「ありがとう。」だけ見える状態を確認。
  - main.gd のクリアパネル中心合わせをオフセット方式に修正済み。修正後の再撮影は
    エンジンビルド完了後に行う（撮影は従来バイナリでも可能だが、問題1の検証と合わせて実施）。
- ビルド1回目: テストのコンパイルエラー（ProjectSettings::load_resource_pack が
  protected）で停止 → call 経由の呼び出しに直して再ビルド。
- 問題1テストの切り分け（重要・記録）:
  - 当初「PCK に生 PNG を入れて res:// で読む」テストがフルスイートでのみ失敗。
    切り分けの結論: (1) ResourceLoader は生 PNG を import 情報なしでは引かない
    （テスト用エディタバイナリでは .import が必須。書き出し版では .import の
    remap 経由で CompressedTexture2D として同じ load_image 分岐を通る）。
    (2) 一見 pass した単体実行は、globalize が相対パスを返し FileAccessPack の
    キー（スキームなし）と偶然一致してフォールバックが拾っただけだった。
    (3) glTF テスト群は ProjectSettings の resource_path を自前 temp に差し替えて
    戻さないため、フルスイートでは globalize が絶対パス化して (2) も成立しない。
  - 対策: テストのパックには ImageTexture をバイナリリソース（.res）として入れ、
    ResourceLoader がパックから直接引ける形で load_image の ResourceLoader 分岐を
    固定した。単体・glTF テストとのペア・UZUPicture スイート全5件 pass を確認。
  - フルスイート: 1577件中 1575 pass、fail は既知の [UDPServer] 2件のみ
    （failed assertions 29件は全てその内側）。終了時の abort もなし。

## 結果（問題ごと）

### 問題1【エンジン】UZUPicture の res:// 読込
- 原因: `load_image` が globalize した実ファイルパスしか開かず、書き出し版では
  PCK 内の画像に届かなかった。
- 修正: scene/3d/uzu_picture.cpp — `ResourceLoader::exists` が真なら Texture2D
  として読み `get_image()`（圧縮は decompress、ミップマップは clear_mipmaps で
  従来経路と挙動を統一）を優先。失敗時のみ従来のファイル読みにフォールバック
  （user://・絶対パス・編集時の loose file は従来どおり）。
  get_image() は RenderingServer から新規コピーを返すため、変形しても
  キャッシュ済みリソースを壊さないことをソースで確認済み。
- 検証: 新規テスト `[SceneTree][UZUPicture] Images served from a resource pack`
  （PCK をマウントし res:// の .res テクスチャで load_image＋build を確認）。
  フルスイート上記結果。書き出し exe での最終確認はリードの再書き出し待ち。

### 問題2【UI配置】左上に壊れて出るパネル
- 原因: 壊れていたのは会話ウィンドウではなくクリアパネル（上記特定のとおり）。
  main.gd `_touch_lantern` の `clear_panel.position = -clear_panel.size / 2.0` が
  親空間座標への配置で、PRESET_CENTER の意図に反し左上隅 (-w/2, -h/2) に
  半分はみ出して置いていた。会話ウィンドウ（PRESET_BOTTOM_WIDE＋オフセット）は
  撮影で正常と確認済みのため変更なし。
- 修正: main.gd — オフセット4辺で中心合わせ（offset_left/top=-size/2、
  offset_right/bottom=+size/2）。
- 検証: 修正前後の撮影比較（~/workspace/hikari-fix-captures/）:
  before_clear.png（左上に半分はみ出し「ありがとう。」のみ可視＝再現）→
  after_clear.png（画面中央に全文表示、HUD も全可視）。
  after_dialogue.png で会話ウィンドウの下部表示も再確認。

### 問題3【タイトルUX】
- 修正: (a) ヒント文言を ui/title_ui.json で「クリック・タップ・Eキー： はじめる」
  に変更し title_ui.tscn を再生成。title.gd が Hint プラークの alpha を
  0.62+0.38·sin で静かに点滅（開始後は消える）。
  (b) 開始入力の即時にヒントを消して「読み込み中…」ラベルを表示し、
  main.tscn を load_threaded_request で非同期読み。_process で status を
  ポーリングし LOADED→change_scene_to_packed、失敗時は change_scene_to_file
  にフォールバック。読み込み中の二重入力は無視。
  ※ ラベル配置は当初 position 指定で書き、レビューで同じ誤り（中心アンカー＋
  position）と気づいてオフセット指定に修正済み。
- 検証: after_title.png（新文言）、loading_label.png（押下2フレーム後:
  ヒント消去＋「読み込み中…」が中央下に表示）、verify_playthrough.py が
  新しい開始経路で pass（下記）。

## 検証まとめ
- エンジン全テスト: 1575/1577 pass（既知 [UDPServer] 2件 fail のみ）。
- verify_playthrough.py: passed=true（2ランとも checkpoints 順序・shards=3・
  cleared=true・開始ハッシュ決定的）。title.gd 最終形でも再実行し pass。
- 撮影証跡: ~/workspace/hikari-fix-captures/（before/after の clear・dialogue・
  title、loading_label）。
