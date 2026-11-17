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
- AI 用のスクリプト（`ai_loop/*.sh`）は Linux の bash 向け。Windows では WSL2（Ubuntu）から使うか、クラウドのセッションを使う。Windows のエディタは、GPU で実際の見た目を確かめるのに使う。
- このスクリプトはクラウド（Linux）で作ったので、Windows での実行はまだ確かめていない。うまくいかない所があれば、出たメッセージを教えてほしい。
