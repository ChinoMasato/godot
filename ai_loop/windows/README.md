# Windows でのローカル開発

`setup.ps1` は、Windows の PC で U.ZU Engine を開発できるようにする準備を、1回で行う。

```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1 -Path "D:\engine work\uzu開発"
```

1. Git・Python・Visual Studio 2022 Build Tools（C++）を winget で入れる（既にあれば飛ばす。`-SkipInstall` で全部飛ばす）。
2. 開発フォルダを作り、`ChinoMasato/godot` の `ai-native` を clone する（既にあれば最新にする）。
3. SCons と zstandard を入れ、エディタ版＋テストをビルドし、単体テストを流す（`-SkipBuild` で飛ばす）。

- Direct3D 12 と AccessKit（スクリーンリーダー対応）は既定で切ってある。どちらも追加の SDK のダウンロードが要るため（2026-11-17、オーナーの PC での最初のビルドがここで止まった）。必要なら `misc\scripts\install_d3d12_sdk_windows.py` と `misc\scripts\install_accesskit.py` を実行してから、`-WithD3D12`／`-WithAccessKit` をつける。エディタは Vulkan で描画する。
- `-Path` を省くと、`ユーザーフォルダ\engine work\uzu開発` に作る。
- このスクリプトはクラウド（Linux）で作ったので、Windows での実行はまだ確かめていない。うまくいかない所があれば、出たメッセージを教えてほしい。

## AI 用のツールを Windows で使う（B-801）

AI 用のツールは `ai_loop/uzu.py`（Python だけで書いたもの）にまとまっていて、Windows でもそのまま動く。`.sh` は Linux 用の薄い包みなので、Windows では `uzu.py` を直接か、`ai_loop\windows\uzu.bat` 経由で呼ぶ。

```bat
cd "D:\engine work\uzu開発\godot"
ai_loop\windows\uzu.bat selftest
ai_loop\windows\uzu.bat engine my_game tree --scene res://main.tscn
ai_loop\windows\uzu.bat look my_game shot --out shot.png
ai_loop\windows\uzu.bat selfplay ai_loop\selfplay\sample_game --input ai_loop\selfplay\sample_replay.json
```

- **エンジンのバイナリ:** `bin\` から自動で探す。`godot.windows.editor.x86_64.console.exe` を優先する。こちらは標準入出力を保つので、AI プレイヤー（B-306）にはこれが要る。別のバイナリを使うときは、環境変数 `GODOT` で指定する。
- **描画:** スクリーンショット、パノラマ、録画は PC の GPU で描画する。Linux のような仮想ディスプレイは要らない。
- **MCP サーバー:** `py -3 ai_loop\mcp_server.py --project <ゲーム>` で使える。Claude Code への登録例: `claude mcp add uzu-engine -- py -3 D:\...\ai_loop\mcp_server.py --project D:\...\my_game`
- **まだ Linux 専用のもの:** ビルドのロックと計測（`build_locked.sh`・`measure.sh`・`agent_*.sh`）。Windows のビルドは `setup.ps1` が受け持つ。
- **確認の状況:** これらのツールはクラウド（Linux）で作って確かめた。Windows での確認はまだなので、`uzu.bat selftest` の結果（最後の数行）を教えてほしい。

## GPU での見た目と重さの確認（Windows）

ビルドしたエディタ（`bin\godot.windows.editor.x86_64.console.exe`）と PC の GPU（Vulkan）で、U.ZU ルックを撮って計る。クラウドの `look.sh shot`（lavapipe）では分からない、本物の GPU での見た目とフレーム時間を確かめる用。

```powershell
# 10種類の見本を撮る（shoot_all.sh と同じ組み合わせ）。out\index.html で並べて見られる
python ai_loop\windows\shoot_gpu.py . out

# 画質ごとの GPU フレーム時間（--quality none|low|standard|high）
bin\godot.windows.editor.x86_64.console.exe --path <見本のコピー> --rendering-driver vulkan --resolution 1920x1080 --script ai_loop\windows\frame_time.gd -- --quality high --frames 300

# 高画質から1つだけ設定を変えて、重さと見た目を比べる（--variant base|no_ssil|no_ssr|no_ssao|no_pcss|pcss_025|taa など）
bin\godot.windows.editor.x86_64.console.exe --path <見本のコピー> --rendering-driver vulkan --resolution 1920x1080 --script ai_loop\windows\look_variant.gd -- --variant taa --shot out.png
```

- 見本のプロジェクトは `ai_loop\look_samples\` をコピーして使う（`--import` を1回流してから）。
- 撮影中はウィンドウが一瞬ずつ開く。
- エディタを開いたままだと、次のビルドの最後（exe の書き込み）が「アクセスが拒否されました」で失敗する。ビルド前にエディタを閉じる。
