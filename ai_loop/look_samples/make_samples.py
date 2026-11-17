#!/usr/bin/env python3
"""Writes the standard look sample projects (BACKLOG B-709) next to this script.

Each project is plain Godot: a camera, geometry with colored materials and one
DirectionalLight3D, without a WorldEnvironment, so it shows the engine's default
look. Agents apply the U.ZU look to a copy (ai_loop/look.sh apply), render it
(look.sh shot) and check it (look.sh check); see ai_loop/look_samples/shoot_all.sh.

  landscape  layered ridges at 150-1400 m, trees: aerial perspective, distance blur
  street     tall buildings, a person-sized capsule, lamp posts: lighting, night, lenses
  materials  leaves, skin, glass and water over a rock, backlit: material presets
"""

import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def rot(elev, azim):
    x, y = math.radians(-elev), math.radians(azim)
    cx, sx, cy, sy = math.cos(x), math.sin(x), math.cos(y), math.sin(y)
    m = [[cy, sy * sx, sy * cx], [0, cx, -sx], [-sy, cy * sx, cy * cx]]
    return ", ".join(f"{v:.6f}" for r in m for v in r)


def write_project(name, resources, nodes):
    d = os.path.join(HERE, name)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "project.godot"), "w") as f:
        f.write(f'config_version=5\n\n[application]\n\nconfig/name="U.ZU look sample: {name}"\nrun/main_scene="res://main.tscn"\n\n[rendering]\n\nrenderer/rendering_method="forward_plus"\n')
    with open(os.path.join(d, "main.tscn"), "w") as f:
        f.write("[gd_scene format=3]\n\n" + "\n".join(resources) + "\n" + "\n".join(nodes))


def mat(id_, color, extra=""):
    return f'[sub_resource type="StandardMaterial3D" id="{id_}"]\nalbedo_color = Color({color}, 1)\n{extra}'


def node(name, type_, transform=None, extra="", parent="."):
    t = f"transform = Transform3D({transform})\n" if transform else ""
    return f'[node name="{name}" type="{type_}" parent="{parent}"]\n{t}{extra}'


def scaled(sx, sy, sz, x, y, z):
    return f"{sx}, 0, 0, 0, {sy}, 0, 0, 0, {sz}, {x}, {y}, {z}"


def landscape():
    res = ['[sub_resource type="PlaneMesh" id="plane"]\nsize = Vector2(4000, 4000)\n', '[sub_resource type="SphereMesh" id="sphere"]\n',
           '[sub_resource type="CylinderMesh" id="trunk"]\ntop_radius = 0.15\nbottom_radius = 0.2\nheight = 2.0\n',
           '[sub_resource type="SphereMesh" id="crown"]\nradius = 1.2\nheight = 2.6\n',
           mat("m_ground", "0.36, 0.42, 0.24", "roughness = 0.95\n"), mat("m_ridge", "0.3, 0.38, 0.26", "roughness = 0.95\n"),
           mat("m_trunk", "0.3, 0.22, 0.15"), mat("m_leaf", "0.2, 0.36, 0.14", "roughness = 0.8\n")]
    nodes = ['[node name="Main" type="Node3D"]\n',
             node("Camera3D", "Camera3D", "1, 0, 0, 0, 0.9961947, -0.0871557, 0, 0.0871557, 0.9961947, 0, 3, 12", "fov = 60.0\n"),
             node("Ground", "MeshInstance3D", extra='mesh = SubResource("plane")\nmaterial_override = SubResource("m_ground")\n')]
    for i, (z, h, d2, w, xo) in enumerate([(-150, 22, 30, 160, -90), (-350, 45, 60, 300, 140), (-700, 80, 110, 520, -200), (-1400, 150, 200, 900, 250)]):
        for k in range(3):
            nodes.append(node(f"Ridge{i}_{k}", "MeshInstance3D", scaled(w, h * 2, d2 * 2, xo + (k - 1) * w * 0.8, 0, z - k * 15), 'mesh = SubResource("sphere")\nmaterial_override = SubResource("m_ridge")\n'))
    for i, (tx, tz) in enumerate([(-5, -2), (-3, -6), (4, -3), (6, -9), (-8, -12), (1, -16), (9, -20), (-12, -24)]):
        nodes.append(node(f"Trunk{i}", "MeshInstance3D", scaled(1, 1, 1, tx, 1, tz), 'mesh = SubResource("trunk")\nmaterial_override = SubResource("m_trunk")\n'))
        nodes.append(node(f"Crown{i}", "MeshInstance3D", scaled(1, 1, 1, tx, 3, tz), 'mesh = SubResource("crown")\nmaterial_override = SubResource("m_leaf")\n'))
    nodes.append(node("Sun", "DirectionalLight3D", f"{rot(50, 150)}, 0, 0, 0", "shadow_enabled = true\n"))
    write_project("landscape", res, nodes)


def street():
    res = ['[sub_resource type="PlaneMesh" id="plane"]\nsize = Vector2(300, 300)\n', '[sub_resource type="BoxMesh" id="box"]\n',
           '[sub_resource type="CapsuleMesh" id="person"]\nradius = 0.3\nheight = 1.7\n',
           '[sub_resource type="CylinderMesh" id="pole"]\ntop_radius = 0.05\nbottom_radius = 0.07\nheight = 3.0\n',
           mat("m_ground", "0.4, 0.39, 0.36", "roughness = 0.9\n"), mat("m_wall", "0.62, 0.58, 0.52"),
           mat("m_person", "0.25, 0.35, 0.55", "roughness = 0.7\n"), mat("m_pole", "0.15, 0.15, 0.16")]
    nodes = ['[node name="Main" type="Node3D"]\n',
             node("Camera3D", "Camera3D", "1, 0, 0, 0, 0.9848078, 0.1736482, 0, -0.1736482, 0.9848078, 0, 2.2, 9"),
             node("Ground", "MeshInstance3D", extra='mesh = SubResource("plane")\nmaterial_override = SubResource("m_ground")\n')]
    for i, (x, z, w, h) in enumerate([(-5, -6, 4, 18), (0.5, -9, 5, 24), (6, -7, 4, 15), (-9, -14, 5, 30), (10, -15, 6, 27)]):
        nodes.append(node(f"Building{i}", "MeshInstance3D", scaled(w, h, 3, x, h / 2, z), 'mesh = SubResource("box")\nmaterial_override = SubResource("m_wall")\n'))
    nodes.append(node("Person", "MeshInstance3D", scaled(1, 1, 1, 0, 0.85, 0), 'mesh = SubResource("person")\nmaterial_override = SubResource("m_person")\n'))
    for i, (x, z) in enumerate([(-2.5, -1.5), (3, -3.5), (-1, -6)]):
        nodes.append(node(f"Pole{i}", "MeshInstance3D", scaled(1, 1, 1, x, 1.5, z), 'mesh = SubResource("pole")\nmaterial_override = SubResource("m_pole")\n'))
    nodes.append(node("Sun", "DirectionalLight3D", f"{rot(50, 330)}, 0, 0, 0", "shadow_enabled = true\n"))
    write_project("street", res, nodes)


def materials():
    res = ['[sub_resource type="PlaneMesh" id="plane"]\nsize = Vector2(400, 400)\n', '[sub_resource type="PlaneMesh" id="leaf"]\nsize = Vector2(0.5, 0.9)\n',
           '[sub_resource type="PlaneMesh" id="pool"]\nsize = Vector2(2.4, 1.6)\n',
           '[sub_resource type="SphereMesh" id="sphere"]\nradius = 0.6\nheight = 1.2\n', '[sub_resource type="SphereMesh" id="rock"]\nradius = 0.8\nheight = 1.0\n',
           mat("m_ground", "0.4, 0.38, 0.3", "roughness = 0.95\n"), mat("m_rock", "0.55, 0.52, 0.46"),
           mat("m_leaf", "0.32, 0.5, 0.18", "roughness = 0.6\ncull_mode = 2\n"), mat("m_skin", "0.85, 0.64, 0.52", "roughness = 0.55\n"),
           mat("m_glass", "0.9, 0.95, 1", "roughness = 0.1\n"), mat("m_water", "0.1, 0.3, 0.33", "roughness = 0.5\n")]
    nodes = ['[node name="Main" type="Node3D"]\n',
             node("Camera3D", "Camera3D", "1, 0, 0, 0, 0.9659258, 0.258819, 0, -0.258819, 0.9659258, 0, 2.6, 6", "fov = 55.0\n"),
             node("Ground", "MeshInstance3D", extra='mesh = SubResource("plane")\nmaterial_override = SubResource("m_ground")\n')]
    for i, (dx, dy, a) in enumerate([(0, 1.8, 10), (0.55, 2.1, -15), (1.1, 1.7, 25), (0.3, 2.6, -5), (0.9, 2.6, 15)]):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        nodes.append(node(f"Leaf{i}", "MeshInstance3D", f"{ca:.6f}, {-sa:.6f}, 0, 0, 0, -1, {sa:.6f}, {ca:.6f}, 0, {-0.55 + dx}, {dy}, 0", 'mesh = SubResource("leaf")\nmaterial_override = SubResource("m_leaf")\n'))
    nodes.append(node("Skin", "MeshInstance3D", scaled(1, 1, 1, -1.7, 0.6, 0.8), 'mesh = SubResource("sphere")\nmaterial_override = SubResource("m_skin")\n'))
    nodes.append(node("Glass", "MeshInstance3D", scaled(1, 1, 1, 1.7, 0.6, 0.8), 'mesh = SubResource("sphere")\nmaterial_override = SubResource("m_glass")\n'))
    nodes.append(node("Rock", "MeshInstance3D", scaled(1, 1, 1, 0, 0.0, 3.3), 'mesh = SubResource("rock")\nmaterial_override = SubResource("m_rock")\n'))
    nodes.append(node("Water", "MeshInstance3D", scaled(1, 1, 1, 0, 0.25, 3.3), 'mesh = SubResource("pool")\nmaterial_override = SubResource("m_water")\n'))
    # Low sun from behind the subjects: backlight shows what passes through.
    nodes.append(node("Sun", "DirectionalLight3D", f"{rot(18, 180)}, 0, 0, 0", "light_color = Color(1, 0.92, 0.8, 1)\nshadow_enabled = true\n"))
    write_project("materials", res, nodes)


if __name__ == "__main__":
    landscape()
    street()
    materials()
    print("written:", ", ".join(sorted(n for n in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, n)))))
