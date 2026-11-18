# LEDGER — 検証ループの実行記録

失敗も消さない。1イテレーション = 1行ブロック。

## 2026-11-17 プロジェクト開始

- フォーク点: Godot master @ 203bf0a（4.8 beta）。ブランチ `ai-native` を作成。
- 環境実測: clone 約2分／477MB。SCons は venv（~/workspace/.venv-build）に導入。
- ビルドプローブ: template_release で4分間に178オブジェクト（約44個/分）。全対象は約2,000ファイル、フルビルド推定1〜1.5時間。
- ベースラインビルド（template_release, tests=yes）をバックグラウンドで開始。結果は完了時にここへ追記する。

## 2026-11-17 — B-004 マルチエージェント開発基盤（Muse、VM）
- 千野の決定: コード開発の主ツールを Claude Code とし、複数 AI エージェントで並行開発できる構成にする。
- 追加: CLAUDE.md（AGENTS.md を @import）、ai_loop/MULTI_AGENT.md、ai_loop/CLAIMS.md、ai_loop/iterations/、agent_start.sh／build_locked.sh（flock 直列化）／agent_finish.sh。
- 実証: agent_start.sh で B-999 テスト用 worktree＋ブランチの作成・削除を確認。スクリプトは bash -n pass。
- この VM にも Claude Code 2.1.325 を導入（~/workspace/tools/claude-code）。初回利用時の Anthropic サインインは千野側の操作が必要。
- ベースラインビルド（B-001）は継続中につき、本件はコード変更なし・運用ファイルのみ。

## 2026-11-17 — GitHub へ展開（A案: 正式フォーク）
- 千野が GitHub で godotengine/godot をフォーク（ChinoMasato/godot、master はフォーク点 203bf0a のまま）。
- Muse の GitHub App にはフォーク作成権限がなかったため、フォーク作成は千野が Web で実施。push はデバイス認証で発行した GitHub CLI トークンで Muse 環境から実行し、push 完了後にトークンファイルは破棄した。
- ai-native を push し、リモート HEAD がローカル HEAD（29765fe）と一致することを ls-remote で確認済み。以後の正は GitHub 側リポジトリとする。

## 2026-11-17 — Codex 対応（千野の指示）
- Codex でもそのまま作業できるよう入口を明示: AGENTS.md 冒頭に「Start here」節を追加（BRIEFING → MULTI_AGENT → BACKLOG の必読順と、ツール別入口: Claude Code=CLAUDE.md／Codex=AGENTS.md）。
- .codex/config.toml を追加（プロジェクト既定値: sandbox_mode=workspace-write、approval_policy=on-request。規則ではなく既定値）。
- BRIEFING.md にツール別入口の注記を追加。規則自体は従来どおり全ツール共通。

## 2026-11-17 — 三者連携基盤（千野の指示）
- 連携窓口を ai_loop/COORDINATION.md に一本化（状況ボード＋質問節＋役割定義）。開始時に読み、終了時に自分の行を更新する運用。正式決定はオーナー承認→リードがLEDGERへ反映した時点で確定、と明記。
- MULTI_AGENT.md に連携節を追加、AGENTS.md と BRIEFING.md の必読リストに COORDINATION.md を追加。
- 背景: Claude Code セッションと Codex が稼働開始し、質問（B-001採用可否・作業ブランチ・分担案）と push 403 の報告がオーナー経由で届いたため、中継を不要にする仕組みとして整備。未決事項は COORDINATION.md の Q1/Q2 に集約。

## 2026-11-17 — Q1 決定（オーナー承認）と B-001 ベースライン採用
- 決定1（B-001）: Claude Code（ローカル）が取得したベースラインを正式採用する。報告値: フルビルド 15分54秒、単体テスト 1431件中 1430 pass、fail 1件は環境起因（Claude Code の報告）。エンジン側コミット ff5246b は運用ファイルのみでコード差分がないため、この結果は現行ツリーに適用できる。詳細記録（ビルド設定・マシン環境・fail 1件のテスト名と根拠）は Claude Code が追って記録し、リードが ai-native へ取り込む。Muse の VM ビルドは本決定により停止した（オブジェクト 2,111 個は保持、増分再開可能。VM 側のテスト結果は得られていない）。
- 決定2（作業ブランチ）: Claude セッション専用ブランチ claude/uzu-engine-briefing-rrno7b を許可。ai-native へのマージはリード経由。
- 決定3（分担）: Claude Code = B-002 → B-003、Codex = B-101 を承認。B-002/B-003 に限り ai_loop/ と version.py へのリード権限を Claude Code へ委譲。B-003 は互換影響の事前調査・報告が条件。
- 連携: 以後の push・マージは保持したデバイス認証トークンでリード（Muse）が随時実施する（オーナー承認済み）。

## 2026-11-17 — B-001 ベースライン確立（Claude Code、クラウド）
- オーナー判断で本結果を正式なベースラインとして採用（Muse 側のビルドは停止）。詳細: ai_loop/iterations/2026-11-17-B-001-claude.md
- ビルド設定: AGENTS.md の規定どおり `scons platform=linuxbsd target=template_release tests=yes accesskit=no arch=x86_64`（並列数だけ -j4。4コアのため）。対象 ai-native @ 3d70ab368。
- マシン: Claude Code クラウドコンテナ（gVisor）、Xeon 2.10GHz × 4コア、RAM 15GB、Ubuntu 24.04.5、gcc 13.3.0、Python 3.13.18、SCons 4.11.1、zstandard 0.25.0。
- 所要時間: フルビルド 15分54秒（2,665 ファイル、exit 0）、単体テスト約31秒。
- テスト: 総数 1431／pass 1430／fail 1／skip 3（assertion 423591 中 1000 が fail）。
- fail: `[IP] resolve_hostname`（tests/core/io/test_ip.cpp:44）、localhost の IPv6 解決が空になる。環境起因と判断した根拠: コンテナの /etc/hosts に `::1 localhost` が無く、getent でも Python の getaddrinfo でも AF_INET6 の localhost を引けない。`::1 localhost` を一時的に足すと全 1431 件 pass、fail 0 になる（そのあと元に戻した）。コード起因の失敗は 0 件。

## 2026-11-17 — リード統合: Claude ブランチを ai-native へマージ
- claude/uzu-engine-briefing-rrno7b（3bb7ea0）を ai-native へ fast-forward マージ。内容: B-001 詳細記録（LEDGER 本編＋iterations）、B-002 measure.sh（リードがレビュー: flock 直列化・失敗時も記録・doctest 集計のパースを確認、bash -n pass）、B-003 事前調査（コード変更なし）。
- Q1 の条件（ベースライン詳細記録）はこれで充足。B-001 は正式に完了。
- B-003 の実装は Q3（COORDINATION.md）のオーナー決定待ち: short_name の扱い（案A: name のみ変更／案B: 両方変更）、判定条件の差し替え、measure.sh の TZ 既定。

## 2026-11-17 — Q3 決定（オーナー承認）: B-003 は案Aパッケージ
- B-003 の範囲を version.py の name のみに確定（short_name は "godot" のまま、互換維持）。BACKLOG の判定を「起動ヘッダの U.ZU Engine 表示＋全テスト pass」に差し替え（--version は名前を含まないため）。
- measure.sh の日付は TZ=Asia/Tokyo を既定化（リードが適用、bash -n pass）。
- B-003 の実装は Claude Code が担当ブランチで実施し、検証（起動ヘッダ＋全テスト）つきで申告する。

## 2026-11-17 — AIファースト改善マップを反映（オーナー指示）
- フォーク実物の棚卸しに基づく改善マップを設計書（第2節末「AIファースト改善マップ」）と BACKLOG へ反映。不足は「出力の構造化」「合否判定」「プレイ自己検証」の3点に集約。原則: 追加モード／オプトインのみで既存挙動を変えない。
- Wave 1 として5件を登録: B-104（ベンチ合否）、B-201（構造化エラーログ試験実装）、B-202（GDScriptエラーJSON）、B-203（プロジェクト検証コマンド）、B-303（自己検証プレイ最小版）。残り候補は BACKLOG の保留へ Wave 2 として集約。

## 2026-11-17 — リード統合: B-003 実装を ai-native へマージ
- version.py の name が "U.ZU Engine" になった（short_name は "godot" のまま）。新規テスト tests/core/test_version.cpp（名前と互換を固定する2件）を含む。
- 検証（Claude Code の最終検証をリードが確認）: 最小プロジェクトで `--headless --quit` の起動ヘッダが "U.ZU Engine v4.8.beta..." と表示され exit 0。全テスト 1433 件中 1432 pass（fail は既知の環境起因 [IP] resolve_hostname のみ。::1 追加時は全件 pass）。変更前は新規テストが fail することも確認済み（red→green）。
- BACKLOG の B-001／B-002／B-003 を完了に更新。Phase 0 は完了。
- 申し送り: 起動ヘッダの website（version.py の website = godotengine.org）の扱いは未決定。必要なら別項目としてオーナー判断。

## 2026-11-17 — B-003 追随: website を U.ZU に変更（オーナー指示）
- version.py の website を https://godotengine.org から https://u-zu.co.jp へ変更（起動ヘッダと About 表示に出る URL）。docs 欄（ドキュメントへの参照）は本家ドキュメントを指すため据え置き。
- tests/core/test_version.cpp に website を固定するテストを1件追加（合計3件）。ヘッダ表示の再検証は次の増分ビルド（Claude Code のループ）で併せて確認する。

## 2026-11-17 — リード統合: Phase 4・Phase 7・A-1・A-3 を ai-native へ（PR #3 相当）
- Claude Code のブランチ（2584812）を ai-native へ fast-forward 統合。オーナーが Q4〜Q7 で直接承認した一連の作業: Phase 7 ルック（B-701〜B-713、B-703b を除く全部。UZULook クラスと look_preset_3d、エディタの U.ZU メニューと構図ガイド、look.sh／palette_check.py／サンプルシーン）、Phase 4（B-401 engine.sh、B-402 MCP サーバー）、A-1（ヘッドレス描画スクリーンショット＋画像差分）、A-4（クラウド環境セットアップ）。
- A-3（= B-102 として完走）: core/io/ip.cpp に RFC 6761 の localhost フォールバック（システム解決が空返しのときだけ loopback を返す。オーナーが案a を選択）。tests/core/io/test_ip.cpp にテスト追加。これでベースライン以来の既知 fail は解消方向。
- 検証状態: Claude Code の申告で全テスト 1476 件 pass（従来 1433＋新規 43）。エディタ側コードは Claude の環境で editor ビルド＋起動確認済み（従来の標準ビルド=template では editor/ がコンパイル対象外だった点が A-2 の動機）。
- A-2 をリードが実施: 標準ループビルドを editor に変更（build_locked.sh／measure.sh は TARGET 環境変数、既定 editor。template_release は実行時・書き出し確認用に温存）。AGENTS.md の Build／Test 節も更新。
- Codex の B-101 はドラフト（fce3e6b）のまま未完（エンジン検証と完了申告待ち）。統合対象外。
- 残: B-703b（UZUPostEffect）が Phase 7 の最後の1件。Phase 2 の Wave 1（B-201〜B-203）と B-104、B-303 は未着手。

## 2026-11-18 — リード統合: WIN-GPU の残りと B-101。影の対策を B-714 として決定
- agent/win-gpu-claude-local の未統合分2件（Windows GPU 道具 shoot_gpu.py／frame_time.gd／look_variant.gd と WIN-GPU 記録）を ai-native へ統合（README の競合は両節を残して解消）。同ブランチの他のコミット（B-703b、B-201〜B-203、B-104、B-301〜B-305、B-502、B-503 draft、B-601、B-103）は Claude Code（クラウド）が ai-native へ直接反映済みだったもの。
- B-101（Codex）を統合: tests/doctest_json.py（doctest XML→JSON）。Codex の実エンジン検証は editor ビルドで 1481 pass／0 fail／3 skip、JSON 一覧と集計の一致を確認。リード側でも変換器の単体テスト10件が pass することを確認した。CLAIMS の競合は行を併せて解消。
- 影の判断（オーナーから委任、WIN-GPU の RTX 4050 実測に基づく）: 「高」の light_angular_distance 0.5→0.25＋影フィルタ High を採用（粒 4.93→1.37、+0.74 ms）。TAA は不採用。B-703b は変更なし。実施は B-714 として BACKLOG に登録（値のテスト更新＋GPU 再計測が判定）。
- 申し送り: テスト総数が環境で異なる（クラウド 1477／Windows 1475／Codex 1481）。Windows との差2件は未特定（全件一覧はオーナー PC の gpu_shots/out/test_cases_windows.txt）。--headless --import の segfault が1回だけ発生（6回再試行で再現せず）。いずれも監視事項。
- 状態メモ: ai-native は Claude Code（クラウド）の直接 push で前進している（B-306、B-504〜B-507、B-801、B-802 を含む）。その運用方針の記録はブランチ側の転記で届いたため、オーナー本人への確認を別途行う。

## 2026-11-18 — 運用方針の確定: 検証済み作業は ai-native へ直接反映
- オーナーがリードとのチャットで明示的に確認: Claude Code は検証（ビルド＋全テスト pass）を通した作業を ai-native へ直接反映してよい（PR・リード統合を待たない）。記録（iterations／CLAIMS）は従来どおり。
- リード（Muse）の役割は、事後レビュー、台帳（LEDGER／BACKLOG／COORDINATION）の整合、判断事項（例: B-714 の影対策）に移る。ブランチ経由で届いた作業（Codex の B-101、ローカルの GPU 確認）は従来どおりリードが統合する。

## 2026-11-18 — Wave 3 を登録（AI 開発特化の次の一手）
- オーナーの問いへのリードの整理を BACKLOG と COORDINATION（Q11）に反映。登録: B-307 状態ハッシュ、B-308 失敗パケット、B-309 セーブステート、B-204 プロジェクトマップ、B-205 UID 修復、B-105 テスト影響選択。推しは検証3点セット（B-307〜309）。Wave 3 候補（リファクタ API 化、意味的 diff、性能予算、要約モード）は保留に集約。

## 2026-11-18 — HD-2D 系テクニックを登録（B-715〜B-722）と B-714 の補記
- オーナーの問いへのリードの整理を BACKLOG（Phase 7 の続き）と COORDINATION（Q12）に反映。推しはチルトシフト DOF（B-715）、Lit スプライト（B-716）、HD-2D ワンクリックプリセット（B-722）。
- 訂正: B-714 は LEDGER 記録時に BACKLOG への転記が漏れていた（アンカー文字列の不一致で置換が効いていなかった）。本日補記した。

## 2026-11-18 — 着手順を提示（リード）
- Phase 8 の完了（B-801〜B-805）を確認。オーナーの「進めて」を受けて、COORDINATION に着手順メモを置いた: B-714 → HD-2D 系（B-715→716→717→722）→ Wave 3（B-307→309→308）→ 隙間で B-105／B-204／B-205。

## 2026-11-18 — Phase 9（U.ZU Picture）を登録。Q13 に回答
- ローカル（Windows／RTX 4050）の試作ブランチ agent/picture-claude-local を ai-native へ統合（GDScript 試作一式、GPU 確認済み、絵は含まず）。
- BACKLOG に Phase 9 を新設: B-903 マップ取り込み（先頭）、B-901 UZUPicture ノード、B-902 PBR 生成、B-904 エージェントコマンド。優先度は現キューの核の後（リード決定、COORDINATION Q13 回答）。
- 方針決定: 推定モデルはエンジンに同梱しない（外部コマンドの口のみ）。分担は C++ 化＝クラウド、GPU 確認＝ローカル。
- なお B-714 はクラウドが実施済み（記録 2026-11-18-B-714-claude.md）。GPU 再計測はローカル待ち。

## 2026-11-18 — リード統合: B-714 GPU 確認、B-105、B-803 補完、B-005
- agent/b714gpu-claude-local を統合: B-714 の GPU 再計測で粒 −72%（7.05→1.99、B-703b 有効時）・+0.71 ms を確認し、B-714 を完了にした。shadow_grain.py を道具に追加。
- agent/b-105-codex を統合: テスト影響選択（tests/select_tests.py）。単体テストはリード側でも pass。BACKLOG の B-105 を完了に。
- agent/b-803-codex を統合: クラウド実装済みの B-803 への Codex の補完（フレームベンチの MCP 公開と不完全な計測の拒否）。mcp_server.py の競合は両方の道具登録を残して解消。Codex が重複に気づいて自実装を取り消した経緯も記録に残っている。
- agent/b-005-codex を統合: オーナー直接依頼のロゴ／アイデンティティ（misc/logo/uzu/、エディタのブランド画像、About の表示とロゴアニメーション、test_pck_packer のロゴサイズ依存の修正）。コードの競合はなく、運用ファイルのみ解消。見た目の最終確認はエディタ実機でオーナーへ。
- 直接反映側では HD-2D の核（B-715〜B-718、B-722）が反映済み（画像はオーナー判断待ち）。

## 2026-11-18 — HD2D の GPU 確認を統合、不具合2件を B-723／B-724 として決定
- agent/hd2dgpu-claude-local を統合（記録、比較画像 hd2d_gpu_*.png、hd2d_gpu.py 等）。全テストは Windows で 1485 pass／0 fail（クラウド 1491 との差3件は未調査のまま）。
- 重さ: ルック「高」4.26 ms → hd2d 適用で 5.88 ms（チルトシフト 0.7〜1.2 ms、粒子はほぼ0）。
- 不具合1（B-716 輪郭）: シェーダーが sprite_texture を repeat で読み、透明な最上段の上に最下段（靴）の輪郭が出る。対策は sampler への repeat_disable（normal_texture も同様）とリードが決定し B-723 に登録。
- 不具合2（B-722）: --no-camera では太陽回転が if (camera) の中にあるため実行されず、夕方に人物が逆光で真っ黒。対策は「リグなしでも現在のカメラ基準で回す。カメラが無ければ回さず結果 JSON で知らせる」と決定し B-724 に登録。

## 2026-11-18 — B-106 上流追随マージの予行演習を登録
- 振り返り（同日）で「勝ち筋の判定は追随マージのコスト」と整理したことを受け、オーナー指示でキューに追加。着手順は不具合修正（B-723／B-724）の次。
- 事前計測: フォーク点からの変更は追加208／既存変更37ファイル。実質的なコード変更は約15ファイルに収まっており、追加型の規律が競合面を小さく保っている。

## 2026-11-18 — B-107 GitHub Actions 検証基盤を登録
- オーナー指示でキューに追加（B-106 の直前）。公開フォークの無料ランナー（4コア／16GB）で editor ビルド＋全テストを自動化する。見込み: コールド約40分、増分10〜20分。
- あわせて記録: Xserver は共用 Web ホスティングのためビルドには使わない（オーナーとの確認事項として COORDINATION に明記）。

## 2026-11-18 — B-108 起動直後のアイコンちらつきを診断・登録
- オーナー報告「起動するとタスクバーに一瞬 Godot のアイコンが出てから U.ZU に切り替わる」。原因: exe 埋め込みアイコン（platform/windows/godot.ico／godot_console.ico）は B-005 の差し替え対象外で Godot のまま。起動直後は埋め込み、初期化後は実行時アイコン（差し替え済み）が表示されるため切り替わる。
- 対策（公式アイコンから .ico を生成して差し替え）を B-108 として登録。小さい修正群（B-723／B-724）と並べて実施する。

## 2026-11-18 — エディタ GUI 方針をオーナーが決定
- 「AI での開発に特化したいのでエディタ画面は不要」というオーナーの方針を、リード整理の形（GUI は作らない／バイナリはヘッドレス基盤として残す／コードは削除しない）で確定。AGENTS.md の Hard rules と COORDINATION の Q14 に記録。
- Q6 の保留だった UI 配置の問いは「UI は作らない」で解消。

## 2026-11-18 — B-723／B-724 完了（GPU 再確認に合格、リードが取り込み）
- ローカル（RTX 4050）が ai-native 8df1b69 で再確認: 全テスト 1490 pass／0 fail／3 skip。B-723 は3人とも頭上の横線が消滅、B-724 は --no-camera の夕方で太陽がカメラ背後に回り顔と服が読める。重さ hd2d 6.12ms／--no-camera 5.54ms（前回とほぼ同じ）、やり直し0回。記録と画像をブランチ agent/b723gpu-claude-local から取り込み、BACKLOG の両項目を完了にした。
- テスト総数の環境差について進展: 今回クラウド 1496 との差6件は **UDSServer のテストのみ**と特定された（件数自体は回により 3〜6 で変動）。監視は継続するが、差の居場所は掴めた。
- 残る小件（記録のみ）: 人物の胴中央の縦の暗い帯（自分の影の可能性）。再現条件が分かったら別途扱う。

## 2026-11-18 — 胴の縦の暗い帯の原因を特定、B-725 として修正を決定
- ローカルが切り分け: 原因は B-716 の BlobShadow（Decal）が人物の板自体にも投影されていたこと（全レイヤー対象＋高さ1m の箱）。影設定・画面効果は無関係と実測で除外。Hero2 の顔の影は Hero3 の実影で仕様どおり。
- リード決定: 推し案（Lit スプライトを専用レイヤーに置き、Decal の cull_mask から外す）を採用し B-725 に登録。高さを縮める別案は地面の凹凸で影が切れる恐れがあるため不採用。記録のみだった小件はこれで正式な修正項目になった。

## 2026-11-18 — B-108 完了（Windows アイコン、オーナー実機確認）
- Codex の agent/b-005-codex-windows を統合: platform/windows/godot.ico と godot_console.ico を U.ZU アイコンに差し替え（生成スクリプト misc/logo/uzu/windows_icons.py を同梱）。あわせて SCsub に .ico へのビルド依存が追加され、アイコン変更時にリソースが再ビルドされるようになった。
- 検証: オーナーが実機で「アイコン直ってます」と確認（統合前のローカルビルド）。これで ai-native から再ビルドしても Godot のアイコンに戻らない。
- 申し送り: 同ブランチの窓口行に「B-720 の GPU 確認はデバイス喪失で一部保留（別ブランチ）」とある。B-720 の完了判定はその回収後に行う。

## 2026-11-18 — PR #4（B-904）と PR #5（B-725）を処理
- PR #4（B-904、Codex）: Picture の試作コマンドを共通 CLI（uzu.py picture）に接続。Windows で回帰16件＋実 Vulkan での検出→採用→撮影を確認済み。競合なしでマージし、B-904 を完了にした。
- PR #5（B-725、Codex）: 人物をレイヤー20に置き Decal の cull_mask から外す修正。新規の影は item_rect × pixel_size から足元へ配置。全テスト 1501 pass（Codex 計測）。ドラフトだったが GPU 再確認が終わったため、リードが ready 化してマージ、B-725 を完了にした。
- ただし B-725 の GPU 再確認（ローカル）は分割判定: 帯は消えた（30.5 → 5.4、合格）が、地面の丸い影は「一度も出ていなかった」ことが判明。保存済みシーンへの再適用で Decal の大きさ・位置が古い値のまま残るのが原因（B-716 の配置に由来する既存の欠陥）。切り分けて B-726 に登録した。
- Phase 9 の B-903／B-901 はクラウドの直接反映で完了済みだったこともあわせて確認した。
