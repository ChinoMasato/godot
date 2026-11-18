"""Run inside Blender after the model script has created the preview scene."""

import sys
from pathlib import Path

import bpy

folder = Path(sys.argv[sys.argv.index("--") + 1])
scene = bpy.context.scene
if not any(obj.type == "MESH" for obj in scene.objects):
    raise ValueError("the model script did not create a mesh")
if scene.camera is None:
    raise ValueError("the model script must set a preview camera")

scene.render.filepath = str(folder / "preview.png")
scene.render.image_settings.file_format = "PNG"
scene.render.use_file_extension = True
if bpy.ops.render.render(write_still=True) != {"FINISHED"}:
    raise RuntimeError("preview render did not finish")

if bpy.ops.export_scene.gltf(
    filepath=str(folder / "model.glb"),
    export_format="GLB",
    use_active_scene=True,
    export_apply=True,
    export_cameras=False,
    export_lights=False,
) != {"FINISHED"}:
    raise RuntimeError("GLB export did not finish")
