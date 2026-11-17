# U.ZU Engine export templates

Export templates built from this fork (they contain U.ZU Engine's own classes, so the
upstream templates cannot replace them). This branch has no shared history with
`ai-native`, so the binaries never enter the main history.

Fetch and use (any OS):

    python3 ai_loop/uzu.py engine <project> templates --fetch
    python3 ai_loop/uzu.py engine <project> export-preset --platform web --template auto

`web/info.json` records the engine commit, the Emscripten version and the build command.
Rebuild and replace when the engine changes in ways games rely on.
