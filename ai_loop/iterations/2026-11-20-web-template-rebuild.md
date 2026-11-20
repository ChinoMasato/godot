# Web テンプレート再ビルド（『灯りの欠片』Web 版が起動しない問題）— 実装エージェント (2026-11-20)

- ブランチ: なし（worktree `~/workspace/godot-webtpl`、ai-native 28edfb57f22 の detached HEAD でビルドのみ。push はリードが行う）
- 変更概要: インストール済み Web 書き出しテンプレートがフォーク初期（2026-11-17）製で Phase 11 以降のエンジンクラスを含まず、『灯りの欠片』の Web 版が GDScript パースエラーで起動しなかった。現在の ai-native から Web テンプレート（nothreads / release）を再ビルドして差し替え、再書き出しした。

## 原因

- 症状: ブラウザコンソールに `Identifier "UZUJuice" not declared`、`"UZUUI" not declared`、`Cannot find member "PARTICLES_FIREFLIES" in base "UZULook"`、`Static function "create_particles()" not found in base "GDScriptNativeClass"` 等のパースエラー（main.gd の reload 時）。画面は真っ暗なまま
- 旧テンプレート `~/.local/share/godot/export_templates/4.8.beta/web_nothreads_release.zip` は 2026-11-17 製のフォーク初期ビルド。wasm 内に "UZUJuice" は 0 件（リード確認済み）
- エディタバイナリでは完走検証 pass（クラスが存在）。Web はテンプレート側ランタイムで動くため別物

## Emscripten 版の確定

- 当初の前提（3.1.64）は 4.x 安定版時代のもの。現行 master の `platform/web/detect.py` は **Emscripten 6.0.1 以上**を要求（dlink+threads は 6.0.12 以上）
- リポジトリ同梱 CI `.github/workflows/web_builds.yml` のピン留めは **EM_VERSION: 6.0.12** → これを採用
- 導入先: `~/workspace/tools/emsdk`（emsdk 本体を git clone --depth 1）

## 手順と結果

1. **emsdk 導入**: `emsdk install 6.0.12` は1回目失敗 — node-v24.19.0 の tar 展開が `Cannot change ownership to uid 1000: Operation not permitted` で終了（この sandbox の既知事情）。`TAR_OPTIONS=--no-same-owner` を付けて再実行し成功（exit 0）。`emsdk activate 6.0.12` 済み、`emcc --version` = **6.0.12** を確認
2. **worktree**: `~/workspace/godot-webtpl` を ai-native 最新 28edfb57f22（remote fork/ai-native と一致確認済み）で作成
3. **テンプレートビルド**: `flock /tmp/uzu-build.lock` の内側で `scons platform=web target=template_release threads=no -j2`（emsdk_env.sh を source、ccache の PATH 前置は AGENTS.md 慣例どおり）。**BUILD_EXIT=0、所要 33分10秒**。生成物 `bin/godot.web.template_release.wasm32.nothreads.zip`（9,491,982 bytes、中身は旧 zip と同じ7ファイル構成、godot.wasm は 31,659,266 bytes）
4. **差し替え**: 旧 zip を `web_nothreads_release.zip.bak-20261117` として保存し、新 zip を `~/.local/share/godot/export_templates/4.8.beta/web_nothreads_release.zip` に配置
5. **再書き出し**: エディタバイナリ `~/workspace/godot-b1219s2/bin/godot.linuxbsd.editor.x86_64` で `ai_loop/samples/hikari` を `--export-release "Web"` し、`~/workspace/hikari-web-latest/` を更新（EXPORT_EXIT=0）。index.wasm は 31,659,266 bytes で新テンプレートの godot.wasm と一致

## 検証結果（機械検証）

新テンプレートの godot.wasm と、再書き出し物の index.wasm の両方で strings 含有を確認（両者同数）:

| 文字列 | 件数 |
|---|---|
| UZUJuice | 5 |
| UZUUI | 11 |
| UZUUIWorld | 5 |
| UZULook | 23 |
| UZUWorldGen | 2 |
| PARTICLES_FIREFLIES | 1 |

- 旧テンプレートでは UZUJuice = 0 件だったものが入った。パースエラーの直接原因（クラス不在）は解消
- ブラウザでの実描画確認はこの VM の既知事情により未実施。u-zu.co.jp への再配置と実機確認はリード側の作業

## 失敗と理由

- emsdk install 1回目: tar の所有権変更エラー（上記）。環境変数で回避済み。今後この VM で emsdk や tar 展開を伴う導入をする場合は `TAR_OPTIONS=--no-same-owner` が要る

## 申し送り

- テンプレートは ai-native が進むたびに古くなる。UZU 系クラスを増やしたフェーズの後は Web テンプレートの再ビルドが必要（今回の手順で約33分＋emsdk は導入済み）
- `~/workspace/godot-webtpl` worktree は残してある（増分再ビルド用）。不要ならリード側で削除可
- godot-merge の作業ツリーには hikari プロジェクトの `.godot/` 変更が残っている（書き出しに伴うもの。本記録ファイル以外は commit していない）
