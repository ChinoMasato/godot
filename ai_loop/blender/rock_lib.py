# -*- coding: utf-8 -*-
"""Procedural rock recipe for the Blender bridge (BACKLOG B-1219, stage 2).

Runs inside Blender (bpy). Driven by ai_loop/blender/rock_main.py,
which ai_loop/rock_recipe.py launches with a fully derived spec (the
cut planes and proportions are derived there from the seed; this
module uses no RNG).

The stage-1 PoC's lesson shapes the recipe: displacement over a smooth
primitive stays an organic blob, so the FORM is made by cutting. A
subdivided icosphere is sliced by half-spaces (boolean differences),
which leaves flat facets meeting at sharp ridges; a small bevel keeps
the ridges from looking CG-sharp; the high-density copy (simple
subdivision + fine displacement, procedural stone) is baked onto the
faceted practical mesh -- albedo / roughness / normal / AO packed as
ORM -- reusing the building route's bake machinery and its pitfalls
(cage + ray distance, sources unhidden, gaps filled, miss rate
measured).

Comparison renders, same framing for all three: (a) the PoC way --
noise displacement only, no cuts; (b) the faceted high model in grey;
(c) the exported practical GLBs re-imported.
"""

import json
import math
import os

import bpy
from mathutils import Matrix, Vector

import building_lib as B


def rock_material(spec, coords_obj):
    """Procedural stone: two noise octaves over the derived tint, a
    fine bump, roughness varying just under 1."""
    mat = bpy.data.materials.new(f"{spec['name']}_stone")
    mat.use_nodes = True
    bsdf = B._bsdf(mat)
    coord = B._tex_coord(mat, coords_obj)
    tint = spec["tint"]
    n1 = B._noise(mat, 2.6, detail=5.0)
    mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
    ramp = B._ramp(mat, [(0.25, (*B._shade(tint, 0.72), 1.0)),
                         (0.62, (*B._shade(tint, 1.0), 1.0)),
                         (0.9, (*B._shade(tint, 1.12), 1.0))])
    mat.node_tree.links.new(n1.outputs["Fac"], ramp.inputs["Fac"])
    n2 = B._noise(mat, 14.0, detail=4.0)
    mat.node_tree.links.new(coord.outputs["Object"], n2.inputs["Vector"])
    varied = B._mix(mat, 'MULTIPLY', 0.30, ramp.outputs["Color"], n2.outputs["Color"])
    mat.node_tree.links.new(varied.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.95
    bump = B._bump(mat, n2.outputs["Fac"], strength=0.4, distance=0.03)
    mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def _cutter(normal, distance, size):
    """A cube covering the half-space {x | normal . x > distance}: its
    inner face lies on the cut plane, its bulk on the removed side."""
    n = Vector(normal)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0))
    cube = bpy.context.active_object
    cube.dimensions = (size, size, size)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    rot = Vector((0, 0, 1)).rotation_difference(n).to_matrix().to_4x4()
    cube.matrix_basis = Matrix.Translation(n * (distance + size / 2)) @ rot
    return cube


def build_faceted(spec, collection):
    """The practical rock: ellipsoid, facet cuts, ground-flattened,
    origin moved to the ground contact, small ridge bevel."""
    r = spec["radius"]
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=r)
    obj = bpy.context.active_object
    obj.name = spec["name"]
    for coll in list(obj.users_collection):
        coll.objects.unlink(obj)
    collection.objects.link(obj)
    obj.scale = tuple(spec["scale"])
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    size = 6.0 * r + 2.0
    for plane in spec["cut_planes"]:
        cube = _cutter(plane["normal"], plane["distance"], size)
        for coll in list(cube.users_collection):
            coll.objects.unlink(cube)
        collection.objects.link(cube)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        mod = obj.modifiers.new("cut", 'BOOLEAN')
        mod.operation = 'DIFFERENCE'
        mod.solver = 'EXACT'
        mod.object = cube
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cube, do_unlink=True)
        obj.select_set(True)
    # sit the rock on its ground cut: origin at the contact plane
    ground_d = spec["cut_planes"][0]["distance"]
    obj.data.transform(Matrix.Translation((0, 0, ground_d)))
    obj.data.update()
    if spec["bevel"] > 0:
        mod = obj.modifiers.new("bev", 'BEVEL')
        mod.width = spec["bevel"]
        mod.segments = 2
        mod.limit_method = 'ANGLE'
        mod.angle_limit = math.radians(30.0)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


def build_noise_only(spec, collection):
    """The stage-1 PoC way, for the comparison render: a subdivided
    sphere with strong displacement and the stone material, no cuts.
    It keeps the ground cut so it sits the same way; everything else
    about it is the smooth blob the PoC recorded as the limit."""
    r = spec["radius"]
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=r)
    obj = bpy.context.active_object
    obj.name = spec["name"] + "_noise"
    for coll in list(obj.users_collection):
        coll.objects.unlink(obj)
    collection.objects.link(obj)
    obj.scale = tuple(spec["scale"])
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    ground_d = spec["cut_planes"][0]["distance"]
    cube = _cutter([0.0, 0.0, -1.0], ground_d, 6.0 * r + 2.0)
    for coll in list(cube.users_collection):
        coll.objects.unlink(cube)
    collection.objects.link(cube)
    bpy.context.view_layer.objects.active = obj
    mod = obj.modifiers.new("cut", 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    mod.object = cube
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cube, do_unlink=True)
    obj.data.transform(Matrix.Translation((0, 0, ground_d)))
    obj.data.update()
    _displace(obj, spec, strength=r * 0.055, levels=3)
    return obj


def _displace(obj, spec, strength, levels):
    tex = bpy.data.textures.new(f"{obj.name}_disp", type='CLOUDS')
    tex.noise_scale = 1.4
    tex.noise_depth = 2
    tex.noise_type = 'SOFT_NOISE'
    off = bpy.data.objects.new(f"{obj.name}_noiseoff", None)
    off.location = tuple(spec["noise_offset"])
    obj.users_collection[0].objects.link(off)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    sub = obj.modifiers.new("dens", 'SUBSURF')
    sub.subdivision_type = 'SIMPLE'
    sub.levels = levels
    bpy.ops.object.modifier_apply(modifier=sub.name)
    disp = obj.modifiers.new("disp", 'DISPLACE')
    disp.texture = tex
    disp.texture_coords = 'OBJECT'
    disp.texture_coords_object = off
    disp.strength = strength
    disp.mid_level = 0.5
    bpy.ops.object.modifier_apply(modifier=disp.name)
    obj.select_set(False)
    return off


def make_high(obj, spec):
    """The bake source: the faceted mesh, simply subdivided (facets
    stay planar) with fine displacement for micro relief."""
    dup = obj.copy()
    dup.data = obj.data.copy()
    dup.name = obj.name + "_high"
    obj.users_collection[0].objects.link(dup)
    _displace(dup, spec, strength=spec["radius"] * 0.018, levels=2)
    return dup


def weld_only(obj):
    """Weld coincident vertices, nothing else. The building reducer's
    flat-dissolve must NOT run on a rock: its facets meet at shallow
    angles by design, and dissolving every edge under 12 degrees
    collapses the form (measured on the first run of this recipe:
    ~600 -> ~75 triangles and a 24-44% bake miss rate, because the
    practical mesh no longer matched the high model)."""
    before = B.tri_count(obj)
    sharp_before = len(B.sharp_edge_sigs(obj))
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)
    after = B.tri_count(obj)
    sharp_after = len(B.sharp_edge_sigs(obj))
    return {"method": "weld only (remove doubles); no flat-dissolve -- on a rock the shallow facet edges are the form",
            "triangles_before": before, "triangles_after": after,
            "sharp_edges_before": sharp_before, "sharp_edges_after": sharp_after,
            "sharp_edge_preservation": 1.0 if sharp_before == 0 else
                round(min(1.0, sharp_after / sharp_before), 4)}


def bake_rock(spec, practical, high, outdir, tex_size):
    """Bake the high rock onto the practical one and export the GLB.
    The same recipe as the building route (building_lib.bake_building),
    minus the parts joinery: cage + ray distance, sources unhidden,
    miss rate measured on the raw bake, gaps filled, ORM packed."""
    stats = {"triangles_practical": B.tri_count(practical),
             "triangles_high": B.tri_count(high)}
    stats["reduction"] = weld_only(practical)
    stats["triangles_practical"] = stats["reduction"]["triangles_after"]

    bpy.context.view_layer.objects.active = practical
    practical.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.005)
    bpy.ops.object.mode_set(mode='OBJECT')

    imgs = {}
    for key, cs in (("albedo", 'sRGB'), ("roughness", 'Non-Color'),
                    ("normal", 'Non-Color'), ("ao", 'Non-Color')):
        img = bpy.data.images.new(f"{spec['name']}_{key}", tex_size, tex_size)
        img.colorspace_settings.name = cs
        imgs[key] = img
    bake_mat = bpy.data.materials.new(f"{spec['name']}_baketarget")
    bake_mat.use_nodes = True
    target = bake_mat.node_tree.nodes.new("ShaderNodeTexImage")
    practical.data.materials.clear()
    practical.data.materials.append(bake_mat)

    cage = practical.copy()
    cage.data = practical.data.copy()
    cage.name = spec["name"] + "_cage"
    bpy.context.scene.collection.objects.link(cage)
    for v in cage.data.vertices:
        v.co += v.normal * B.BAKE_CAGE_EXTRUSION

    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = B.BAKE_SAMPLES
    scene.cycles.device = 'CPU'
    bake = scene.render.bake
    bake.margin = B.BAKE_MARGIN
    bake.use_selected_to_active = True
    bake.max_ray_distance = B.BAKE_RAY_DISTANCE
    bake.use_cage = True
    bake.cage_object = cage
    # Rocks differ from buildings here (measured on boulder_big, plain
    # high model): with the cage at 0.05 and the ray distance at 0.35,
    # the two largest facets miss entirely (13.5% of island texels);
    # cage extrusion 0.15 makes it 48%, extrusion 0.001 makes it 0%,
    # and an unlimited ray distance makes it 0% too -- over a large
    # flat facet the cage sheet sits far enough along the ray that the
    # building route's distance cap cuts the rays short. A rock is
    # convex, so an unlimited ray still takes the near surface first.
    bake.max_ray_distance = 0.0
    high.hide_render = False
    high.hide_viewport = False
    practical.hide_render = False
    practical.hide_viewport = False
    bpy.ops.object.select_all(action='DESELECT')
    high.select_set(True)
    practical.select_set(True)
    bpy.context.view_layer.objects.active = practical

    bake.use_pass_direct = False
    bake.use_pass_indirect = False
    bake.use_pass_color = True
    for key, btype in (("albedo", 'DIFFUSE'), ("roughness", 'ROUGHNESS'),
                       ("normal", 'NORMAL'), ("ao", 'AO')):
        target.image = imgs[key]
        bake_mat.node_tree.nodes.active = target
        if btype == 'NORMAL':
            bake.normal_space = 'TANGENT'
        bpy.ops.object.bake(type=btype)

    stats["bake_miss_rate"] = B.bake_miss_rate(practical, imgs["albedo"])
    B.fill_gaps(imgs["albedo"])
    B.fill_gaps(imgs["roughness"], flat=(0.9, 0.9, 0.9))
    B.fill_gaps(imgs["normal"], flat=(0.5, 0.5, 1.0))
    B.fill_gaps(imgs["ao"], flat=(1.0, 1.0, 1.0))
    if tex_size > B.EXPORT_TEX_SIZE:
        for img in imgs.values():
            img.scale(B.EXPORT_TEX_SIZE, B.EXPORT_TEX_SIZE)
        stats["texture_size"] = B.EXPORT_TEX_SIZE
    imgs["orm"] = B.pack_orm(imgs["ao"], imgs["roughness"], f"{spec['name']}_orm")
    stats["orm_texture"] = f"{spec['name']}_orm.png"
    for key, img in imgs.items():
        img.filepath_raw = os.path.join(outdir, f"{spec['name']}_{key}.png")
        img.save()

    mat = bpy.data.materials.new(f"{spec['name']}_baked")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]

    def tex_node(img):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = img
        return n

    a = tex_node(imgs["albedo"])
    nt.links.new(a.outputs["Color"], bsdf.inputs["Base Color"])
    orm_node = tex_node(imgs["orm"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(orm_node.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    settings = nt.nodes.new("ShaderNodeGroup")
    settings.node_tree = B._gltf_settings_group()
    nt.links.new(sep.outputs["Red"], settings.inputs["Occlusion"])
    n = tex_node(imgs["normal"])
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(n.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    practical.data.materials.clear()
    practical.data.materials.append(mat)
    stats["model_quality"] = B.model_quality_metrics(practical)

    bpy.data.objects.remove(cage, do_unlink=True)
    bake.use_cage = False
    glb_path = os.path.join(outdir, spec["name"] + ".glb")
    bpy.ops.object.select_all(action='DESELECT')
    practical.select_set(True)
    bpy.context.view_layer.objects.active = practical
    bpy.ops.export_scene.gltf(filepath=glb_path, export_format='GLB',
                              use_selection=True, export_apply=True,
                              export_cameras=False, export_lights=False)
    stats["glb"] = os.path.basename(glb_path)
    stats["glb_bytes"] = os.path.getsize(glb_path)
    stats["bake_texture_size"] = tex_size
    if "texture_size" not in stats:
        stats["texture_size"] = tex_size
    return stats


def setup_scene(samples):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = B.RENDER_RES
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    world = bpy.data.worlds.new("W")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.10, 0.12, 0.16, 1.0)
    bg.inputs[1].default_value = 0.8
    sun_data = bpy.data.lights.new("Sun", type='SUN')
    sun_data.energy = 3.2
    sun_data.angle = 0.25
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(52), math.radians(8), math.radians(38))
    scene.collection.objects.link(sun)
    fill_data = bpy.data.lights.new("Fill", type='AREA')
    fill_data.energy = 900.0
    fill_data.shape = 'DISK'
    fill_data.size = 9.0
    fill = bpy.data.objects.new("Fill", fill_data)
    fill.location = (-7, 6, 7)
    fill.rotation_euler = (Vector((0, 0, 2)) - fill.location).to_track_quat('-Z', 'Y').to_euler()
    scene.collection.objects.link(fill)
    bpy.ops.mesh.primitive_plane_add(size=80, location=(0, 0, -0.02))
    ground = bpy.context.active_object
    ground.name = "Ground"
    ground.data.materials.append(B.plain_material("GroundMat", (0.16, 0.15, 0.13)))
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.lens = 40
    cam = bpy.data.objects.new("Cam", cam_data)
    cam.location = (8, -11, 5)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return scene


def run(spec, outdir):
    os.makedirs(outdir, exist_ok=True)
    tex_size = int(spec.get("texture_size", 2048))
    samples = int(spec.get("render_samples", 48))
    rocks = spec["rocks"]
    stats = {"rocks": [], "renders": {}}

    B.clean_scene()
    for r in rocks:
        coll = bpy.data.collections.new("R_" + r["name"])
        bpy.context.scene.collection.children.link(coll)
        coords = bpy.data.objects.new("Coords_" + r["name"], None)
        coll.objects.link(coords)
        practical = build_faceted(r, coll)
        practical.data.materials.append(rock_material(r, coords))
        high = make_high(practical, r)
        rstats = bake_rock(r, practical, high, outdir, tex_size)
        entry = {"name": r["name"]}
        entry.update(rstats)
        stats["rocks"].append(entry)
        print(f"BUILT {r['name']}: {json.dumps(entry)}")

    layout = []
    x = 0.0
    for r in rocks:
        layout.append({"name": r["name"], "x": round(x, 3), "y": 0.0, "flip": False})
        x += 2.8 * r["radius"] + 1.2
    total = x - 1.2
    for e in layout:
        e["x"] = round(e["x"] - total / 2 + 1.4 * rocks[0]["radius"], 3)
    stats["layout"] = layout
    at = {e["name"]: e for e in layout}

    # (a) the PoC way: noise only
    B.clean_scene()
    scene = setup_scene(samples)
    for r in rocks:
        coll = bpy.data.collections.new("N_" + r["name"])
        scene.collection.children.link(coll)
        coords = bpy.data.objects.new("Coords_" + r["name"], None)
        coll.objects.link(coords)
        obj = build_noise_only(r, coll)
        obj.data.materials.append(rock_material(r, coords))
        B.parent_at([obj], "Noise_" + r["name"], at[r["name"]])
    B.frame_camera(scene)
    path = os.path.join(outdir, "render_a_noise_only.png")
    B.render_to(scene, path)
    stats["renders"]["a_noise_only"] = os.path.basename(path)

    # (b) faceted high model, plain grey
    B.clean_scene()
    scene = setup_scene(samples)
    grey = B.gray_material()
    for r in rocks:
        coll = bpy.data.collections.new("H_" + r["name"])
        scene.collection.children.link(coll)
        coords = bpy.data.objects.new("Coords_" + r["name"], None)
        coll.objects.link(coords)
        practical = build_faceted(r, coll)
        high = make_high(practical, r)
        bpy.data.objects.remove(practical, do_unlink=True)
        high.data.materials.clear()
        high.data.materials.append(grey)
        B.parent_at([high], "High_" + r["name"], at[r["name"]])
    B.frame_camera(scene)
    path = os.path.join(outdir, "render_b_faceted_gray.png")
    B.render_to(scene, path)
    stats["renders"]["b_faceted_gray"] = os.path.basename(path)

    # (c) the exported GLBs, re-imported
    B.clean_scene()
    scene = setup_scene(samples)
    for r in rocks:
        glb = os.path.join(outdir, r["name"] + ".glb")
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=glb)
        imported = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
        B.parent_at(imported, "Baked_" + r["name"], at[r["name"]])
    B.frame_camera(scene)
    path = os.path.join(outdir, "render_c_baked.png")
    B.render_to(scene, path)
    stats["renders"]["c_baked"] = os.path.basename(path)

    stats_path = os.path.join(outdir, "stats.json")
    with open(stats_path, "w", encoding="utf-8") as fh:
        json.dump(stats, fh, indent=2, ensure_ascii=False)
    print("UZU_ROCK_STATS " + stats_path)
    return stats
