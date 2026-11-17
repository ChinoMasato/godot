# U.ZU Engine identity

Based on the official U.ZU company identity, at the owner's request on
2026-11-17 (Japan time).

![Motion preview](preview.gif)

## Sources

- Header identity: <https://u-zu.co.jp/assets/uzu-identity.svg>
- Application icon: <https://u-zu.co.jp/assets/uzu-icon.svg>
- Original downloaded sources: `official-identity.svg`, `official-icon.svg`.
- `wordmark-paths.svg`: vector tracing of the wordmark PNG embedded in the
  official header SVG. Used to remove the embedded-image/mask dependency from
  engine SVGs. The circular mark's paths and metallic gradient come directly
  from the official identity.

Existing engine copyright, license, and upstream logo attribution files are
retained.

## Engine integration

- `main/app_icon.png`: runtime application/window icon, official icon variant.
- `main/splash.png`, `main/splash_editor.png`: static default engine/editor
  splash, official header identity plus an outlined ENGINE subtitle.
- `editor/icons/Logo.svg`, `GodotFile.svg`: separate wordmark and circular mark
  used by the About dialog. Existing internal icon identifiers are retained.
- `editor/icons/TitleBarLogo.svg`, `Godot.svg`, `GodotMonochrome.svg`: editor
  title-bar and small icon variants.
- `editor/icons/DefaultProjectIcon.svg`: the official icon for newly created
  projects. Existing projects' saved/custom icons are not overwritten.
- `misc/logo/` PNG/SVG files and `misc/dist/html/logo.svg`: distributed artwork.

The About dialog turns only the circular mark once in 2.4 seconds using
smoothstep easing. Text stays fixed. Processing stops after the turn and when
the dialog is hidden; reopening restarts the motion. Splash images stay static
and do not add startup delays. Game/project settings and compatibility names
are unchanged.

This does not regenerate native platform packaging files (`.ico`, `.icns`,
Apple Icon Composer layers), or overwrite existing games' custom/project
icons. Those are distinct distribution assets.

Editor logo variants supply the official navy background so their light
metallic colors remain legible with light editor themes.

## Preview and regeneration

Open `preview.html` in a browser for replay/stop controls. Its initial animation
honors reduced-motion preferences and stops when the document is hidden. The
GIF repeats for viewing; the actual About animation runs once per opening.

```sh
# SVG exports and self-contained preview; Python standard library only.
python3 misc/logo/uzu/generate_assets.py

# Also export the PNGs using Playwright and /usr/bin/chromium.
python3 misc/logo/uzu/generate_assets.py --raster
```

The website is never changed or fetched by regeneration. The generator renders
the stored artwork; it does not need VTracer to run. VTracer 0.6.15 was used once
to produce the stored wordmark paths from the official embedded image.

## Actual editor smoke check

[Dark editor](editor-preview.png) · [Light editor](editor-preview-light.png)

With a graphical display, open the isolated `verify/` project in the new
editor. Its temporary verification plugin opens the actual About dialog,
samples rotation, checks that processing stops after the turn and when hidden,
and checks that reopening restarts it. It leaves the dialog open for inspection.

```sh
UZU_LOGO_REPORT=/tmp/uzu-logo-smoke.json UZU_LOGO_AUTOCLOSE=1 \
  bin/godot.linuxbsd.editor.x86_64 --editor --path misc/logo/uzu/verify
```

`UZU_LOGO_REPORT` defaults to `user://uzu-logo-smoke.json`. With
`UZU_LOGO_AUTOCLOSE=1`, the verification editor exits 20 seconds after writing
the result, with code 0 for a passing check or 1 for a failed check. Without
that flag it stays open. Set `UZU_LOGO_SCREENSHOT` to a PNG path to capture
the actual About viewport after the animation settles. Use an isolated editor
profile for automated runs.
