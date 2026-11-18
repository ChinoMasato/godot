# 上流追随の監視リスト（B-106、2026-11-18 更新）

フォーク点 203bf0a から、このフォークが**既存のファイル**を変えた一覧（40 ファイル）。上流をマージするとき、ここに挙がったファイルが上流でも変わっていたら、自動マージの結果を必ず目で確かめる。新しく足したファイル（ai_loop/、uzu_*、テストなど）は上流と衝突しない。

上流の変更と重なったファイル（2026-11-18 の予行）: editor/doc/editor_help.cpp（自動マージで両立。上流は「ファイルマネージャーで開く」の文言、フォークは B-806 の判定）。

```
core/io/ip.cpp
doc/classes/LightmapGI.xml
editor/doc/editor_help.cpp
editor/editor_node.cpp
editor/file_system/editor_file_system.cpp
editor/file_system/editor_file_system.h
editor/gui/editor_about.cpp
editor/gui/editor_about.h
editor/icons/DefaultProjectIcon.svg
editor/icons/Godot.svg
editor/icons/GodotFile.svg
editor/icons/GodotMonochrome.svg
editor/icons/Logo.svg
editor/icons/TitleBarLogo.svg
editor/project_manager/project_manager.cpp
editor/scene/3d/node_3d_editor_plugin.cpp
editor/scene/3d/node_3d_editor_plugin.h
editor/scene/3d/node_3d_editor_viewport.cpp
editor/scene/3d/node_3d_editor_viewport.h
main/app_icon.png
main/main.cpp
main/splash.png
misc/dist/html/editor.html
misc/dist/html/logo.svg
misc/logo/icon.png
misc/logo/icon.svg
misc/logo/icon_outlined.png
misc/logo/icon_outlined.svg
misc/logo/logo.png
misc/logo/logo.svg
misc/logo/logo_outlined.png
misc/logo/logo_outlined.svg
scene/3d/lightmap_gi.cpp
scene/3d/lightmap_gi.h
scene/register_scene_types.cpp
servers/rendering/SCsub
servers/rendering/dummy/storage/texture_storage.h
tests/core/io/test_ip.cpp
tests/core/io/test_pck_packer.cpp
version.py
```
