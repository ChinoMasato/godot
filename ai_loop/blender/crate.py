"""Build an original timber supply crate and a small preview rig."""

import bpy
from mathutils import Vector

scene = bpy.context.scene


def material(name, color, roughness):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Roughness"].default_value = roughness
    return mat


def box(name, location, dimensions, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = obj.modifiers.new("Soft corners", "BEVEL")
    bevel.width = 0.018
    bevel.segments = 2
    obj.data.materials.append(mat)


wood = material("Honey wood", (0.43, 0.23, 0.105), 0.74)
trim = material("Dark trim", (0.19, 0.075, 0.029), 0.7)
for index in range(4):
    z = 0.16 + index * 0.23
    box(f"Board_{index + 1}", (0, 0, z), (1.28, 0.95, 0.216), wood)
for x in (-0.52, 0.52):
    for y in (-0.49, 0.49):
        box("Corner brace", (x, y, 0.51), (0.14, 0.075, 0.97), trim)
for x in (-0.52, 0.52):
    box("Lid brace", (x, 0, 0.984), (0.14, 0.99, 0.064), trim)

focus = Vector((0, 0, 0.48))
bpy.ops.object.camera_add(location=(2.8, -3.8, 2.4))
scene.camera = bpy.context.object
scene.camera.rotation_euler = (focus - scene.camera.location).to_track_quat("-Z", "Y").to_euler()
scene.camera.data.type = "ORTHO"
scene.camera.data.ortho_scale = 2.34
for location, power, size in [((2, -3, 5), 450, 4), ((-3, 1, 3), 260, 3)]:
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.object
    light.rotation_euler = (focus - light.location).to_track_quat("-Z", "Y").to_euler()
    light.data.energy = power
    light.data.shape = "DISK"
    light.data.size = size
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 12
scene.cycles.use_denoising = False
scene.render.resolution_x = 384
scene.render.resolution_y = 384
scene.render.resolution_percentage = 100
