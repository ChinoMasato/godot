# UZUVfx sample (BACKLOG B-1109)

A minimal scene that fires U.ZU Engine's recipe-driven visual effects:
the built-in `explosion` recipe and a laser built from a recipe dictionary
(`beam` + impact `sparks` + `light` + `sfx`), both from `main.gd` with
`UZUVfx.fire_named` / `UZUVfx.fire_between`.

Run it:

```sh
bin/godot.linuxbsd.editor.x86_64 --path ai_loop/samples/vfx
```

Screenshot (software rendering, no GPU needed):

```sh
python3 ai_loop/uzu.py look ai_loop/samples/vfx shot --out /tmp/vfx.png --frames 13
```

Validate the recipes without running anything:

```sh
python3 ai_loop/uzu.py engine ai_loop/samples/vfx vfx --name explosion
python3 ai_loop/uzu.py engine ai_loop/samples/vfx vfx-recipes
```
