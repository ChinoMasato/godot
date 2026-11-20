# Image-first sample (B-1221)

`illustration.png` is the specification image for this sample project: a
dusk village street, drawn procedurally by `make_illustration.py`
(standard library only, so its provenance is exact -- it is not an
AI-generated image; the flow treats any PNG the same).

Generate the scaffold, build it, and walk it:

```sh
python3 ai_loop/uzu.py image-scene ai_loop/samples/image_scene \
    --image ai_loop/samples/image_scene/illustration.png --shot shot.png
```

This writes `image_scene/` inside the project (layer textures, the
B-1207 `spec.json`, the generation `report.json`, `walk_targets.json`),
builds `image_scene/image_scene.tscn` from the spec, and walks the
scaffold's waypoints with a capsule walker. To grow the scene, edit
`image_scene/spec.json` and rebuild with `uzu.py build`.
