"""Original courier robot with a skinned rig and two in-place animation clips."""

from math import radians

import bpy
from mathutils import Vector

scene = bpy.context.scene
scene.render.fps = 24
rig = bpy.data.objects.new("ScoutRig", bpy.data.armatures.new("ScoutSkeleton"))
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")


def bone(name, head, tail, parent=None):
    item = rig.data.edit_bones.new(name)
    item.head = head
    item.tail = tail
    if parent:
        item.parent = rig.data.edit_bones[parent]


bone("Root", (0, 0, 0), (0, 0, 0.3))
bone("Body", (0, 0, 1.0), (0, 0, 1.58), "Root")
bone("Head", (0, 0, 1.58), (0, 0, 1.94), "Body")
for side, sign in (("L", 1), ("R", -1)):
    x = 0.46 * sign
    hip = 0.2 * sign
    bone(f"UpperArm.{side}", (x, 0, 1.46), (x, 0, 1.13), "Body")
    bone(f"Forearm.{side}", (x, 0, 1.13), (x, 0, 0.81), f"UpperArm.{side}")
    bone(f"Thigh.{side}", (hip, 0, 0.99), (hip, 0, 0.56), "Root")
    bone(f"Shin.{side}", (hip, 0, 0.56), (hip, 0, 0.15), f"Thigh.{side}")
bpy.ops.object.mode_set(mode="OBJECT")


def material(name, color, metal=0.0, emission=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Metallic"].default_value = metal
    shader.inputs["Roughness"].default_value = 0.46
    if emission:
        shader.inputs["Emission Color"].default_value = (*color, 1)
        shader.inputs["Emission Strength"].default_value = emission
    return mat


shell = material("Ochre enamel", (0.78, 0.32, 0.055), 0.35)
dark = material("Slate joints", (0.037, 0.08, 0.11), 0.28)
lights = material("Mint indicators", (0.12, 0.84, 0.65), emission=0.7)
parts = []


def box(name, location, dimensions, mat, group):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    part = bpy.context.object
    part.name = name
    part.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = part.modifiers.new("Rounded casing", "BEVEL")
    bevel.width = min(dimensions) * 0.18
    bevel.segments = 3
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    part.data.materials.append(mat)
    part.vertex_groups.new(name=group).add(list(range(len(part.data.vertices))), 1, "REPLACE")
    parts.append(part)


box("Body shell", (0, 0, 1.28), (0.68, 0.38, 0.55), shell, "Body")
box("Belt", (0, 0, 1.04), (0.62, 0.37, 0.12), dark, "Root")
box("Pack", (0, 0.24, 1.29), (0.51, 0.22, 0.42), dark, "Body")
box("Head casing", (0, 0, 1.75), (0.64, 0.43, 0.36), shell, "Head")
box("Visor", (0, -0.217, 1.77), (0.52, 0.056, 0.21), dark, "Head")
for sign in (-1, 1):
    box("Eye", (0.122 * sign, -0.25, 1.77), (0.087, 0.026, 0.071), lights, "Head")
box("Antenna stem", (0.23, 0.07, 1.96), (0.045, 0.045, 0.18), dark, "Head")
box("Antenna tip", (0.23, 0.07, 2.07), (0.075, 0.075, 0.065), lights, "Head")
for side, sign in (("L", 1), ("R", -1)):
    x, hip = 0.46 * sign, 0.20 * sign
    box("Upper arm", (x, 0, 1.31), (0.19, 0.21, 0.29), dark, f"UpperArm.{side}")
    box("Forearm", (x, -0.015, 0.99), (0.23, 0.24, 0.29), shell, f"Forearm.{side}")
    box("Thigh", (hip, 0, 0.77), (0.24, 0.24, 0.3), dark, f"Thigh.{side}")
    box("Shin", (hip, 0, 0.36), (0.24, 0.25, 0.31), shell, f"Shin.{side}")
    box("Boot", (hip, -0.064, 0.13), (0.28, 0.42, 0.15), dark, f"Shin.{side}")

bpy.ops.object.select_all(action="DESELECT")
for part in parts:
    part.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
mesh = bpy.context.object
mesh.name = "Scout"
mesh.parent = rig
mesh.modifiers.new("Scout skin", "ARMATURE").object = rig

animation = rig.animation_data_create()


def key_pose(frame, rotations, lift=0.0):
    for joint in rig.pose.bones:
        joint.rotation_mode = "XYZ"
        joint.rotation_euler = tuple(radians(a) for a in rotations.get(joint.name, (0, 0, 0)))
        joint.keyframe_insert(data_path="rotation_euler", frame=frame, group=joint.name)
    root = rig.pose.bones["Root"]
    root.location.y = lift  # Root's local Y points upward in the rest pose.
    root.keyframe_insert(data_path="location", frame=frame, group=root.name)


def clip(name, poses):
    for frame, rotations, lift in poses:
        key_pose(frame, rotations, lift)
    action = animation.action
    action.name = name
    animation.action = None
    track = animation.nla_tracks.new()
    track.name = name
    track.strips.new(name, 1, action)
    track.mute = True
    return track


idle = clip(
    "IdleLoop",
    [
        (1, {}, 0),
        (25, {"Body": (-4, 0, 0), "Head": (0, -12, 0)}, 0.027),
        (49, {}, 0),
    ],
)
walk = []
for frame, swing, passing, lift in (
    (1, 25, 0, 0),
    (7, 0, 1, 0.055),
    (13, -25, 0, 0),
    (19, 0, -1, 0.055),
    (25, 25, 0, 0),
):
    pose = {"Body": (-5, 0, 0), "Head": (5, 0, 0)}
    for side, sign in (("L", 1), ("R", -1)):
        pose[f"Thigh.{side}"] = (swing * sign - 12 * max(sign * passing, 0), 0, 0)
        pose[f"Shin.{side}"] = (7 + 37 * max(sign * passing, 0), 0, 0)
        pose[f"UpperArm.{side}"] = (-0.77 * swing * sign, 0, 0)
        pose[f"Forearm.{side}"] = (-21 + 5 * sign * passing, 0, 0)
    walk.append((frame, pose, lift))
clip("WalkLoop", walk)
idle.mute = False
scene.frame_set(25)
scene.frame_start = 1
scene.frame_end = 49

focus = Vector((0, 0, 1.07))
bpy.ops.object.camera_add(location=(3.2, -5.7, 2.7))
scene.camera = bpy.context.object
scene.camera.rotation_euler = (focus - scene.camera.location).to_track_quat("-Z", "Y").to_euler()
scene.camera.data.type = "ORTHO"
scene.camera.data.ortho_scale = 2.75
for position, power, color in (((-3, -4, 5), 520, (1, 0.85, 0.68)), ((2, 1, 3), 410, (0.57, 0.84, 1))):
    bpy.ops.object.light_add(type="AREA", location=position)
    lamp = bpy.context.object
    lamp.rotation_euler = (focus - lamp.location).to_track_quat("-Z", "Y").to_euler()
    lamp.data.energy = power
    lamp.data.color = color
    lamp.data.size = 3
scene.render.engine = "CYCLES"
scene.cycles.samples = 20
scene.cycles.use_denoising = False
scene.render.resolution_x = 400
scene.render.resolution_y = 400
scene.render.resolution_percentage = 100
