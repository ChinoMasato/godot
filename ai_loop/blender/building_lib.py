# -*- coding: utf-8 -*-
"""Procedural building recipe for the Blender bridge (BACKLOG B-1219, stage 1).

Runs inside Blender (bpy). Driven by ai_loop/blender/building_main.py,
which ai_loop/building_recipe.py launches with a fully derived spec
(every variation is derived there from the seed; this module uses no RNG,
so the same spec always builds the same buildings).

Pipeline per building (the B-1219 route proven by the rock PoC):
  1. construction -- segment-built walls with real openings (no boolean
     cuts), window frames / mullions / sills, a recessed door or a shop
     front, a stepped foundation, and a gable / hip / flat roof with
     eaves. Openings and relief are geometry, never a texture trick.
  2. high-density copy -- SIMPLE subdivision + fine displacement on the
     wall / roof / foundation parts, procedural node materials.
  3. bake -- the practical copy is joined, Smart-UV unwrapped, and the
     high version's albedo / roughness / normal are baked onto it
     (selected-to-active). The two bake pitfalls from the PoC are
     defaults here: a cage pushed out along the normals plus a generous
     ray distance (without them the albedo gets pure-black miss spots),
     and bake sources are force-unhidden first (Cycles refuses to bake
     objects that are disabled for rendering). The bake miss rate
     (near-black texels inside the UV islands) is measured and reported.
  4. export -- the practical building as one GLB with the baked PBR set,
     origin at the centre of the building's base.

Also builds the comparison street: (a) houses in the current
UZUWorldGen style (flat box walls, window quads floating on the wall,
prism roof, flat colours -- a Blender reproduction of the C++ recipe in
modules/uzu_worldgen), (b) the recipe buildings at high density in
plain grey, (c) the exported practical GLBs re-imported. All three use
the same street, camera and lighting.
"""

import json
import math
import os

import bpy
from mathutils import Vector

# Bake defaults (PoC lessons, see module docstring).
BAKE_SAMPLES = 4  # colour-only diffuse / roughness / normal carry no GI noise
EXPORT_TEX_SIZE = 1024  # baked textures are downscaled to this for export
BAKE_MARGIN = 3  # small: fill_gaps closes the atlas; a wide margin floods small islands
BAKE_RAY_DISTANCE = 0.25
BAKE_CAGE_EXTRUSION = 0.06

RENDER_RES = (960, 540)


def tri_count(obj):
    me = obj.data
    return sum(len(p.vertices) - 2 for p in me.polygons)


def clean_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# ------------------------------------------------------------------ materials

def _bsdf(mat):
    return mat.node_tree.nodes["Principled BSDF"]


def _tex_coord(mat, coords_obj):
    node = mat.node_tree.nodes.new("ShaderNodeTexCoord")
    node.object = coords_obj
    return node


def _noise(mat, scale, detail=5.0, roughness=0.8):
    n = mat.node_tree.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail
    n.inputs["Roughness"].default_value = roughness
    return n


def _ramp(mat, stops):
    node = mat.node_tree.nodes.new("ShaderNodeValToRGB")
    elements = node.color_ramp.elements
    elements[0].position, elements[0].color = stops[0]
    elements[1].position, elements[1].color = stops[1]
    for pos, col in stops[2:]:
        el = elements.new(pos)
        el.color = col
    return node


def _mix(mat, blend, fac, a, b):
    node = mat.node_tree.nodes.new("ShaderNodeMixRGB")
    node.blend_type = blend
    node.inputs[0].default_value = fac
    mat.node_tree.links.new(a, node.inputs[1])
    mat.node_tree.links.new(b, node.inputs[2])
    return node


def _pattern_uv(mat, coord):
    """(along-surface, up) 2D coordinates for the 2D Brick texture.

    TexBrick patterns the X-Y plane of its input, so raw object
    coordinates are wrong on a wall: the pattern's second axis lands
    on the wall's depth, and the courses collapse into stripes (with
    mortar patches wherever displacement shifts the depth). Build
    the input from the surface normal instead: u = object X on
    Y-facing surfaces, object Y on X-facing ones; v = object Z.
    On sloped roof slabs the dominant axis wins, which keeps the
    courses horizontal in world space.
    """
    nt = mat.node_tree

    def math(op, a=None, b=None):
        node = nt.nodes.new("ShaderNodeMath")
        node.operation = op
        for idx, val in ((0, a), (1, b)):
            if val is None:
                continue
            if isinstance(val, bpy.types.NodeSocket):
                nt.links.new(val, node.inputs[idx])
            else:
                node.inputs[idx].default_value = val
        return node.outputs[0]

    pos = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Object"], pos.inputs["Vector"])
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    nrm = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["Normal"], nrm.inputs["Vector"])
    sel = math('GREATER_THAN', math('ABSOLUTE', nrm.outputs["Y"]),
               math('ABSOLUTE', nrm.outputs["X"]))
    u = math('ADD', math('MULTIPLY', sel, pos.outputs["X"]),
             math('MULTIPLY', math('SUBTRACT', 1.0, sel), pos.outputs["Y"]))
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(u, comb.inputs["X"])
    nt.links.new(pos.outputs["Z"], comb.inputs["Y"])
    comb.inputs["Z"].default_value = 0.0
    return comb.outputs["Vector"]


def _bump(mat, height_out, strength=0.3, distance=0.05):
    node = mat.node_tree.nodes.new("ShaderNodeBump")
    node.inputs["Strength"].default_value = strength
    node.inputs["Distance"].default_value = distance
    mat.node_tree.links.new(height_out, node.inputs["Height"])
    return node


def _shade(rgb, f):
    """Scale an (r, g, b) tint (0..1 floats, as derived by the CLI)."""
    return tuple(max(0.0, min(1.0, v * f)) for v in rgb[:3])


def make_materials(spec, coords_obj):
    """Procedural PBR materials for one building, tinted from the spec."""
    links_of = {}
    tint = spec["wall_tint"]
    roof_tint = spec["roof_tint"]
    mats = {}

    def new(name):
        mat = bpy.data.materials.new(f"{spec['name']}_{name}")
        mat.use_nodes = True
        mats[name] = mat
        return mat

    # ---- wall materials -------------------------------------------------
    if spec["wall"] == "plaster":
        mat = new("wall")
        bsdf = _bsdf(mat)
        coord = _tex_coord(mat, coords_obj)
        n1 = _noise(mat, 3.5)
        mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
        ramp = _ramp(mat, [(0.25, (*_shade(tint, 0.90), 1.0)),
                           (0.80, (*_shade(tint, 1.04), 1.0))])
        mat.node_tree.links.new(n1.outputs["Fac"], ramp.inputs["Fac"])
        mat.node_tree.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.92
        n2 = _noise(mat, 30.0, detail=4.0)
        mat.node_tree.links.new(coord.outputs["Object"], n2.inputs["Vector"])
        bump = _bump(mat, n2.outputs["Fac"], strength=0.12, distance=0.03)
        mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    elif spec["wall"] == "brick":
        mat = new("wall")
        bsdf = _bsdf(mat)
        coord = _tex_coord(mat, coords_obj)
        brick = mat.node_tree.nodes.new("ShaderNodeTexBrick")
        brick.offset = 0.0
        brick.offset_frequency = 2
        brick.inputs["Color1"].default_value = (*_shade(tint, 1.0), 1.0)
        brick.inputs["Color2"].default_value = (*_shade(tint, 0.78), 1.0)
        brick.inputs["Mortar"].default_value = (0.62, 0.58, 0.52, 1.0)
        brick.inputs["Mortar Size"].default_value = 0.04
        brick.inputs["Mortar Smooth"].default_value = 0.004
        brick.inputs["Bias"].default_value = -0.25
        # TexBrick scales its input x5 internally (measured: width 1.0
        # over a 0..1 range gives 5 bricks), so real-world sizes are x5.
        brick.inputs["Brick Width"].default_value = 0.44 * 5
        brick.inputs["Row Height"].default_value = 0.105 * 5
        mat.node_tree.links.new(_pattern_uv(mat, coord), brick.inputs["Vector"])
        n1 = _noise(mat, 6.0, detail=4.0)
        mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
        varied = _mix(mat, 'MULTIPLY', 0.35, brick.outputs["Color"], n1.outputs["Color"])
        mat.node_tree.links.new(varied.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.86
        bump = _bump(mat, brick.outputs["Fac"], strength=0.5, distance=0.02)
        mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    else:  # wood siding: horizontal bands + grain
        mat = new("wall")
        bsdf = _bsdf(mat)
        coord = _tex_coord(mat, coords_obj)
        wave = mat.node_tree.nodes.new("ShaderNodeTexWave")
        wave.wave_type = 'BANDS'
        wave.bands_direction = 'Z'
        wave.wave_profile = 'SIN'
        wave.inputs["Scale"].default_value = 4.4  # ~0.23 m per board
        wave.inputs["Distortion"].default_value = 1.2
        wave.inputs["Detail"].default_value = 3.0
        mat.node_tree.links.new(coord.outputs["Object"], wave.inputs["Vector"])
        ramp = _ramp(mat, [(0.0, (*_shade(tint, 0.62), 1.0)),
                           (0.18, (*_shade(tint, 1.0), 1.0)),
                           (0.92, (*_shade(tint, 1.06), 1.0)),
                           (1.0, (*_shade(tint, 0.55), 1.0))])
        mat.node_tree.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
        n1 = _noise(mat, 18.0, detail=5.0)
        mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
        varied = _mix(mat, 'MULTIPLY', 0.30, ramp.outputs["Color"], n1.outputs["Color"])
        mat.node_tree.links.new(varied.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.78
        bump = _bump(mat, wave.outputs["Fac"], strength=0.35, distance=0.02)
        mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    # ---- roof ------------------------------------------------------------
    mat = new("roof")
    bsdf = _bsdf(mat)
    coord = _tex_coord(mat, coords_obj)
    if spec["roof"] == "flat":
        n1 = _noise(mat, 5.0, detail=4.0)
        mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
        ramp = _ramp(mat, [(0.3, (*_shade(roof_tint, 0.85), 1.0)),
                           (0.75, (*_shade(roof_tint, 1.1), 1.0))])
        mat.node_tree.links.new(n1.outputs["Fac"], ramp.inputs["Fac"])
        mat.node_tree.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.96
    else:  # tile courses from a brick pattern
        brick = mat.node_tree.nodes.new("ShaderNodeTexBrick")
        brick.offset = 0.5
        brick.offset_frequency = 2
        brick.inputs["Color1"].default_value = (*_shade(roof_tint, 1.0), 1.0)
        brick.inputs["Color2"].default_value = (*_shade(roof_tint, 0.8), 1.0)
        brick.inputs["Mortar"].default_value = (*_shade(roof_tint, 0.55), 1.0)
        brick.inputs["Mortar Size"].default_value = 0.02
        brick.inputs["Mortar Smooth"].default_value = 0.003
        brick.inputs["Bias"].default_value = -0.2
        brick.inputs["Brick Width"].default_value = 0.30 * 5  # x5: TexBrick internal scale
        brick.inputs["Row Height"].default_value = 0.15 * 5
        mat.node_tree.links.new(_pattern_uv(mat, coord), brick.inputs["Vector"])
        mat.node_tree.links.new(brick.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.88
        bump = _bump(mat, brick.outputs["Fac"], strength=0.7, distance=0.03)
        mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    # ---- trim / frames / door -------------------------------------------
    frame_col = (0.16, 0.10, 0.06) if spec["wall"] != "brick" else (0.88, 0.84, 0.76)
    mat = new("trim")
    bsdf = _bsdf(mat)
    bsdf.inputs["Base Color"].default_value = (*frame_col, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.62
    coord = _tex_coord(mat, coords_obj)
    n1 = _noise(mat, 22.0, detail=4.0)
    mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
    bump = _bump(mat, n1.outputs["Fac"], strength=0.15, distance=0.015)
    mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    mat = new("door")
    bsdf = _bsdf(mat)
    bsdf.inputs["Base Color"].default_value = (*_shade(frame_col, 0.8), 1.0)
    bsdf.inputs["Roughness"].default_value = 0.58

    # ---- glass ------------------------------------------------------------
    mat = new("glass")
    bsdf = _bsdf(mat)
    bsdf.inputs["Base Color"].default_value = (0.05, 0.08, 0.11, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.07

    # ---- stone (foundation, steps, chimney cap) ---------------------------
    mat = new("stone")
    bsdf = _bsdf(mat)
    coord = _tex_coord(mat, coords_obj)
    n1 = _noise(mat, 4.0, detail=6.0)
    mat.node_tree.links.new(coord.outputs["Object"], n1.inputs["Vector"])
    ramp = _ramp(mat, [(0.3, (0.30, 0.29, 0.27, 1.0)), (0.75, (0.52, 0.50, 0.46, 1.0))])
    mat.node_tree.links.new(n1.outputs["Fac"], ramp.inputs["Fac"])
    mat.node_tree.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.98
    voro = mat.node_tree.nodes.new("ShaderNodeTexVoronoi")
    voro.feature = 'DISTANCE_TO_EDGE'
    voro.inputs["Scale"].default_value = 3.0
    mat.node_tree.links.new(coord.outputs["Object"], voro.inputs["Vector"])
    bump = _bump(mat, voro.outputs["Distance"], strength=0.5, distance=0.04)
    mat.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    # ---- brick for the chimney (always brick, whatever the walls are) -----
    mat = new("brick")
    bsdf = _bsdf(mat)
    coord = _tex_coord(mat, coords_obj)
    brick = mat.node_tree.nodes.new("ShaderNodeTexBrick")
    brick.offset_frequency = 2
    brick.inputs["Color1"].default_value = (0.52, 0.27, 0.20, 1.0)
    brick.inputs["Color2"].default_value = (0.42, 0.21, 0.16, 1.0)
    brick.inputs["Mortar"].default_value = (0.60, 0.56, 0.50, 1.0)
    brick.inputs["Mortar Size"].default_value = 0.012
    brick.inputs["Brick Width"].default_value = 0.44
    brick.inputs["Row Height"].default_value = 0.105
    mat.node_tree.links.new(coord.outputs["Object"], brick.inputs["Vector"])
    mat.node_tree.links.new(brick.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.88

    return mats


def gray_material():
    mat = bpy.data.materials.new("RecipeGray")
    mat.use_nodes = True
    bsdf = _bsdf(mat)
    bsdf.inputs["Base Color"].default_value = (0.52, 0.52, 0.53, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.94
    return mat


def plain_material(name, color, roughness=0.95):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = _bsdf(mat)
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


# ------------------------------------------------------------------ builder

from mathutils import Matrix  # noqa: E402


class Builder:
    """Builds one building at the local origin, front facing -Y.

    Parts share centred mesh datablocks (one per distinct size and
    material) and carry their placement on the object transform, so a
    part's world position is its building-local position and parts can
    be joined, baked and parented without transform surprises. The
    repetition of a few part meshes is what gives the building its
    density.
    """

    def __init__(self, spec, collection, coords_obj):
        self.spec = spec
        self.collection = collection
        self.mats = make_materials(spec, coords_obj)
        self.parts = []  # list of (obj, detail_flag)
        self._mesh_cache = {}  # (dims, bevel, material) -> shared box mesh
        self._prism_count = 0
        self.W = spec["width"]
        self.D = spec["depth"]
        self.T = spec["wall_thickness"]
        self.base = spec["foundation_height"]
        self.top = self.base + spec["floors"] * spec["floor_height"]

    # -- low-level part makers -------------------------------------------
    def _link(self, obj):
        for coll in obj.users_collection:
            coll.objects.unlink(obj)
        self.collection.objects.link(obj)

    def _box_mesh(self, dims, bevel, mat):
        """The shared mesh for a box part: centred at the origin, bevel
        applied, material attached. Identical parts (every window's
        jambs, glass, mullions and sills, the floor bands, ...) reuse
        ONE mesh datablock -- density by repetition, not by unique
        geometry. Placement lives on the object transform instead."""
        key = (tuple(round(float(d), 4) for d in dims), round(float(bevel), 4),
               mat.name if mat is not None else None)
        mesh = self._mesh_cache.get(key)
        if mesh is not None:
            return mesh
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0))
        tmp = bpy.context.active_object
        tmp.dimensions = dims
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        if bevel > 0:
            mod = tmp.modifiers.new("bev", 'BEVEL')
            mod.width = min(bevel, min(dims) * 0.3)
            mod.segments = 1
            bpy.context.view_layer.objects.active = tmp
            bpy.ops.object.modifier_apply(modifier=mod.name)
        mesh = tmp.data
        mesh.name = f"{self.spec['name']}_box{len(self._mesh_cache)}"
        if mat is not None:
            mesh.materials.append(mat)
        bpy.data.objects.remove(tmp, do_unlink=True)
        self._mesh_cache[key] = mesh
        return mesh

    def unique_mesh_count(self):
        """Distinct mesh datablocks behind this building's parts."""
        return len(self._mesh_cache) + self._prism_count

    def add_box(self, name, center, dims, mat, detail=False, bevel=0.02, rot_x=0.0):
        obj = bpy.data.objects.new(f"{self.spec['name']}_{name}",
                                   self._box_mesh(dims, bevel, mat))
        m = Matrix.Translation(center)
        if rot_x:
            m = m @ Matrix.Rotation(rot_x, 4, 'X')
        obj.matrix_basis = m
        self._link(obj)
        self.parts.append((obj, detail))
        return obj

    def add_prism(self, name, verts, faces, mat, detail=False):
        mesh = bpy.data.meshes.new(f"{self.spec['name']}_{name}")
        mesh.from_pydata(verts, [], faces)
        mesh.update()
        obj = bpy.data.objects.new(f"{self.spec['name']}_{name}", mesh)
        self.collection.objects.link(obj)
        if mat is not None:
            mesh.materials.append(mat)
        self.parts.append((obj, detail))
        self._prism_count += 1
        return obj

    def _roof_swap(self):
        return self.D > self.W

    def add_roof_box(self, name, center, dims, mat, detail=False, bevel=0.02, rot_x=0.0):
        """A box in the canonical roof frame (ridge along X, span along Y).

        When the building is deeper than wide, the finished part is
        rotated 90 degrees about the building's Z axis so the ridge runs
        along the longer side: canonical (x, y) -> building (-y, x).
        """
        obj = self.add_box(name, center, dims, mat, detail, bevel, rot_x)
        if self._roof_swap():
            obj.matrix_basis = Matrix.Rotation(math.radians(90), 4, 'Z') @ obj.matrix_basis
        return obj

    def add_roof_prism(self, name, verts, faces, mat, detail=False):
        if self._roof_swap():
            verts = [(-y, x, z) for (x, y, z) in verts]
        return self.add_prism(name, verts, faces, mat, detail)

    # -- walls --------------------------------------------------------------
    def _wall_box(self, wall, u0, u1, o0, o1, z0, z1, mat, detail=True, bevel=0.015):
        """A box in wall frame: u along the wall, o outward from its centre
        plane, z up. Walls: front (-Y), back (+Y), left (-X), right (+X)."""
        cu, co, cz = (u0 + u1) / 2, (o0 + o1) / 2, (z0 + z1) / 2
        du, do, dz = abs(u1 - u0), abs(o1 - o0), abs(z1 - z0)
        if min(du, do, dz) < 0.005:
            return None
        if wall == "front":
            center, dims = (cu, -self.D / 2 - co, cz), (du, do, dz)
        elif wall == "back":
            center, dims = (cu, self.D / 2 + co, cz), (du, do, dz)
        elif wall == "left":
            center, dims = (-self.W / 2 - co, cu, cz), (do, du, dz)
        else:  # right
            center, dims = (self.W / 2 + co, cu, cz), (do, du, dz)
        return self.add_box(f"{wall}", center, dims, mat, detail, bevel)

    def build_wall(self, wall, openings):
        """Partition the wall into NON-OVERLAPPING boxes around the
        openings: slice along the wall at every opening edge, and in
        each strip fill only the z-gaps between the openings that cover
        the strip. (The naive layout -- full-height columns plus a
        below/above box per opening -- double-covers the wall wherever
        openings on different floors share an x-range: two solids with
        coincident front faces, and a ray that lands on the farther
        face shades pitch black from inside the nearer solid.)"""
        length = self.W if wall in ("front", "back") else self.D
        T, z0, z1 = self.T, self.base, self.top
        wall_mat = self.mats["wall"]
        cuts = {-length / 2, length / 2}
        for o in openings:
            cuts.add(round(o["x"] - o["w"] / 2, 6))
            cuts.add(round(o["x"] + o["w"] / 2, 6))
        cuts = sorted(cuts)
        for a, b in zip(cuts, cuts[1:]):
            if b - a < 0.005:
                continue
            mid = (a + b) / 2
            spans = sorted((o["z"], o["z"] + o["h"]) for o in openings
                           if o["x"] - o["w"] / 2 - 1e-6 <= mid <= o["x"] + o["w"] / 2 + 1e-6)
            cur = z0
            for s, e in spans:
                if s - cur > 0.005:
                    self._wall_box(wall, a, b, -T / 2, T / 2, cur, s, wall_mat)
                cur = max(cur, e)
            if z1 - cur > 0.005:
                self._wall_box(wall, a, b, -T / 2, T / 2, cur, z1, wall_mat)
        for o in openings:
            self.dress_opening(wall, o)

    def dress_opening(self, wall, o):
        T = self.T
        x, z, w, h = o["x"], o["z"], o["w"], o["h"]
        trim, glass = self.mats["trim"], self.mats["glass"]
        f = 0.07  # frame width
        o_in, o_out = T / 2 - 0.06, T / 2 + 0.02  # frame band around outer face
        if o["type"] in ("window", "shop_glass"):
            # frame: jambs + header + sill cover
            self._wall_box(wall, x - w / 2 - f, x - w / 2, o_in, o_out, z, z + h, trim, False, 0.008)
            self._wall_box(wall, x + w / 2, x + w / 2 + f, o_in, o_out, z, z + h, trim, False, 0.008)
            self._wall_box(wall, x - w / 2 - f, x + w / 2 + f, o_in, o_out, z + h, z + h + f, trim, False, 0.008)
            self._wall_box(wall, x - w / 2 - f, x + w / 2 + f, o_in, o_out, z - f, z, trim, False, 0.008)
            # sill, protruding
            self._wall_box(wall, x - w / 2 - 0.08, x + w / 2 + 0.08,
                           T / 2 - 0.02, T / 2 + 0.07, z - 0.05, z, self.mats["stone"], False, 0.01)
            # glass, recessed to the middle of the wall
            self._wall_box(wall, x - w / 2 + 0.01, x + w / 2 - 0.01, -0.012, 0.012,
                           z + 0.01, z + h - 0.01, glass, False, 0.0)
            # mullions
            if o["type"] == "window":
                self._wall_box(wall, x - 0.02, x + 0.02, 0.0, 0.03, z, z + h, trim, False, 0.0)
                self._wall_box(wall, x - w / 2, x + w / 2, 0.0, 0.03,
                               z + h / 2 - 0.02, z + h / 2 + 0.02, trim, False, 0.0)
            else:  # shop front: two dividers, a sign board and a canopy slab
                for dx in (-w / 6, w / 6):
                    self._wall_box(wall, x + dx - 0.025, x + dx + 0.025, 0.0, 0.035,
                                   z, z + h, trim, False, 0.0)
                self._wall_box(wall, x - w / 2 - 0.15, x + w / 2 + 0.15,
                               T / 2, T / 2 + 0.14, z + h + 0.10, z + h + 0.50, trim, False, 0.01)
                self._wall_box(wall, x - w / 2 - 0.2, x + w / 2 + 0.2,
                               T / 2, T / 2 + 0.78, z + h + 0.03, z + h + 0.09,
                               self.mats["roof"], False, 0.01)
        elif o["type"] == "door":
            self._wall_box(wall, x - w / 2 - f, x - w / 2, o_in, o_out, z, z + h, trim, False, 0.008)
            self._wall_box(wall, x + w / 2, x + w / 2 + f, o_in, o_out, z, z + h, trim, False, 0.008)
            self._wall_box(wall, x - w / 2 - f, x + w / 2 + f, o_in, o_out, z + h, z + h + f, trim, False, 0.008)
            # panel, recessed toward the inside
            self._wall_box(wall, x - w / 2 + 0.02, x + w / 2 - 0.02,
                           -T / 2 + 0.03, -T / 2 + 0.08, z, z + h - 0.01, self.mats["door"], False, 0.01)
            # knob
            self._wall_box(wall, x + w / 2 - 0.20, x + w / 2 - 0.12,
                           -T / 2 + 0.08, -T / 2 + 0.13, z + 1.0, z + 1.08, trim, False, 0.0)

    # -- roofs ---------------------------------------------------------------
    def build_roof(self):
        spec = self.spec
        A = max(self.W, self.D)   # ridge direction (canonical X)
        B = min(self.W, self.D)   # span (canonical Y)
        top, rise, eaves = self.top, spec["roof_rise"], spec["eaves"]
        roof_mat = self.mats["roof"]
        if spec["roof"] == "gable":
            half = B / 2 + eaves
            theta = math.atan2(rise, half)
            slope = math.hypot(half, rise) + 0.18
            dims = (A + 0.7, slope, 0.12)
            self.add_roof_box("roof_f", (0, -half / 2, top + rise / 2 + 0.02), dims,
                              roof_mat, True, 0.01, rot_x=theta)
            self.add_roof_box("roof_b", (0, half / 2, top + rise / 2 + 0.02), dims,
                              roof_mat, True, 0.01, rot_x=-theta)
            # gable infill: triangular prisms at both ridge ends
            for sx in (-1, 1):
                cx = sx * (A / 2 - 0.05)
                t = 0.06
                prof = [(-B / 2, top), (B / 2, top), (0.0, top + rise)]
                verts = [(cx - t, y, z) for (y, z) in prof] + [(cx + t, y, z) for (y, z) in prof]
                faces = [(2, 1, 0), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]
                self.add_roof_prism("gable", verts, faces, self.mats["wall"], True)
            # ridge cap
            self.add_roof_box("ridge", (0, 0, top + rise + 0.05), (A + 0.72, 0.2, 0.07),
                              self.mats["trim"], False, 0.01)
        elif spec["roof"] == "hip":
            e = eaves
            self.add_roof_box("eave_slab", (0, 0, top), (A + 2 * e, B + 2 * e, 0.08),
                              self.mats["trim"], False, 0.01)
            hx, hy, ez = A / 2 + e, B / 2 + e, top + 0.04
            rl = max(0.0, A - B)
            r0, r1 = (-rl / 2, 0.0, ez + rise), (rl / 2, 0.0, ez + rise)
            c = [(-hx, -hy, ez), (hx, -hy, ez), (hx, hy, ez), (-hx, hy, ez)]
            if rl < 0.01:
                verts = c + [r0]
                faces = [(3, 2, 1, 0),          # bottom
                         (0, 1, 4),             # front
                         (1, 2, 4),             # right
                         (2, 3, 4),             # back
                         (3, 0, 4)]             # left
            else:
                verts = c + [r0, r1]
                faces = [(3, 2, 1, 0),          # bottom
                         (0, 1, 5, 4),          # front trapezoid
                         (2, 3, 4, 5),          # back trapezoid
                         (1, 2, 5),             # right triangle
                         (3, 0, 4)]             # left triangle
            self.add_roof_prism("hip", verts, faces, roof_mat, True)
        else:  # flat: slab + parapet
            self.add_box("roof_slab", (0, 0, top + 0.07), (self.W + 0.26, self.D + 0.26, 0.14),
                         roof_mat, True, 0.01)
            wall_mat = self.mats["wall"]
            pz = top + 0.14 + 0.17
            self.add_box("parapet_f", (0, -(self.D / 2 + 0.08), pz), (self.W + 0.26, 0.1, 0.34), wall_mat, False, 0.01)
            self.add_box("parapet_b", (0, self.D / 2 + 0.08, pz), (self.W + 0.26, 0.1, 0.34), wall_mat, False, 0.01)
            self.add_box("parapet_l", (-(self.W / 2 + 0.08), 0, pz), (0.1, self.D + 0.26, 0.34), wall_mat, False, 0.01)
            self.add_box("parapet_r", (self.W / 2 + 0.08, 0, pz), (0.1, self.D + 0.26, 0.34), wall_mat, False, 0.01)
        # chimney on the ridge (canonical frame), brick whatever the walls
        if spec.get("chimney") and spec["roof"] != "flat":
            cx = spec["chimney_x"]
            h = rise + 1.15
            self.add_roof_box("chimney", (cx, 0, top - 0.25 + h / 2), (0.48, 0.48, h),
                              self.mats["brick"], False, 0.01)
            self.add_roof_box("chimney_cap", (cx, 0, top - 0.25 + h + 0.045), (0.62, 0.62, 0.09),
                              self.mats["stone"], False, 0.01)

    # -- whole building -------------------------------------------------------
    def build(self):
        spec = self.spec
        # foundation: a stepped stone base, the building sits on it
        self.add_box("foundation", (0, 0, self.base / 2),
                     (self.W + 0.36, self.D + 0.36, self.base), self.mats["stone"], True, 0.03)
        for wall in ("front", "back", "left", "right"):
            self.build_wall(wall, spec["openings"].get(wall, []))
        # string courses between floors (not on wood siding)
        if spec["wall"] != "wood":
            for fl in range(1, spec["floors"]):
                z = self.base + fl * spec["floor_height"]
                self.add_box("band_f", (0, -self.D / 2, z), (self.W + 0.08, self.T + 0.06, 0.1),
                             self.mats["stone"], False, 0.01)
                self.add_box("band_b", (0, self.D / 2, z), (self.W + 0.08, self.T + 0.06, 0.1),
                             self.mats["stone"], False, 0.01)
                self.add_box("band_l", (-self.W / 2, 0, z), (self.T + 0.06, self.D + 0.08, 0.1),
                             self.mats["stone"], False, 0.01)
                self.add_box("band_r", (self.W / 2, 0, z), (self.T + 0.06, self.D + 0.08, 0.1),
                             self.mats["stone"], False, 0.01)
        self.build_roof()
        # a stone step in front of the front door
        for o in spec["openings"].get("front", []):
            if o["type"] == "door":
                self.add_box("step", (o["x"], -self.D / 2 - self.T / 2 - 0.22, 0.09),
                             (1.5, 0.6, 0.18), self.mats["stone"], False, 0.02)
        return self.parts

    # -- high-density copy ------------------------------------------------------
    def make_high(self):
        """A high-density copy of the parts: SIMPLE subdivision + fine
        displacement on the detail parts. All parts share one local frame
        (identity object transforms), so the displacement noise is
        continuous across part seams."""
        tex = bpy.data.textures.new(f"{self.spec['name']}_disp", type='CLOUDS')
        tex.noise_scale = 2.2
        tex.noise_depth = 2
        tex.noise_type = 'SOFT_NOISE'
        # legacy textures expose no offset; the per-seed offset rides on
        # an OBJECT-coords empty shared by every part of this building
        off = bpy.data.objects.new(f"{self.spec['name']}_noiseoff", None)
        off.location = tuple(self.spec["noise_offset"])
        self.collection.objects.link(off)
        high = []
        for obj, detail in self.parts:
            dup = obj.copy()
            dup.data = obj.data.copy()
            dup.name = obj.name + "_high"
            self.collection.objects.link(dup)
            if detail:
                bpy.context.view_layer.objects.active = dup
                dup.select_set(True)
                sub = dup.modifiers.new("dens", 'SUBSURF')
                sub.subdivision_type = 'SIMPLE'
                sub.levels = 2
                bpy.ops.object.modifier_apply(modifier=sub.name)
                disp = dup.modifiers.new("disp", 'DISPLACE')
                disp.texture = tex
                disp.texture_coords = 'OBJECT'
                disp.texture_coords_object = off
                disp.strength = 0.012
                disp.mid_level = 0.5
                bpy.ops.object.modifier_apply(modifier=disp.name)
                dup.select_set(False)
            high.append(dup)
        return high


# ------------------------------------------------------------------ bake

def _uv_island_mask(obj, grid=256):
    """Rasterise the object's UV layout into a boolean grid (numpy).

    Used to measure the bake miss rate only over texels that are inside
    a UV island; texels outside islands are supposed to stay empty.
    """
    import numpy as np
    mask = np.zeros((grid, grid), dtype=bool)
    me = obj.data
    uv = me.uv_layers.active.data
    for poly in me.polygons:
        pts = [tuple(uv[li].uv) for li in poly.loop_indices]
        for k in range(1, len(pts) - 1):
            tri = [pts[0], pts[k], pts[k + 1]]
            xs = [p[0] * grid for p in tri]
            ys = [p[1] * grid for p in tri]
            x0, x1 = max(0, int(min(xs))), min(grid - 1, int(max(xs)) + 1)
            y0, y1 = max(0, int(min(ys))), min(grid - 1, int(max(ys)) + 1)
            if x0 > x1 or y0 > y1:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, ay), (bx, by), (cx, cy) = [(p[0] * grid, p[1] * grid) for p in tri]
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            w0 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
            w1 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
            w2 = 1.0 - w0 - w1
            mask[y0:y1 + 1, x0:x1 + 1] |= (w0 >= -0.01) & (w1 >= -0.01) & (w2 >= -0.01)
    return mask


def bake_miss_rate(joined, albedo_img):
    """Fraction of near-black texels inside the UV islands of the albedo.

    A texel the bake rays missed stays pure black (the PoC's symptom);
    none of the recipe materials has a legitimate near-black albedo
    (the darkest, glass, is 0.05/0.08/0.11), so black inside an island
    means a miss. Measured on a 256 grid sampled from the baked image.
    """
    import numpy as np
    grid = 256
    mask = _uv_island_mask(joined, grid)
    total = int(mask.sum())
    if total == 0:
        return None
    w, h = albedo_img.size
    px = np.asarray(albedo_img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    gx, gy = np.meshgrid(((np.arange(grid) + 0.5) / grid * (w - 1)).astype(int),
                         ((np.arange(grid) + 0.5) / grid * (h - 1)).astype(int))
    lum = px[gy, gx][..., :3].max(axis=2)
    return float((lum[mask] < 0.015).sum() / total)


def fill_gaps(img, flat=None):
    """Fill every unbaked texel (alpha 0) with plausible colour.

    The bake margin only dilates 16 px; a building atlas has hundreds
    of small islands with wider gaps, and mip sampling at any distance
    pulls the black gaps into the surface (black bars, washed-out
    windows). Fill from a low-res pyramid so the gaps take the nearest
    island colours, then sharpen with a few full-res passes. Normal
    maps pass flat=(0.5, 0.5, 1.0): smeared normals are worse than flat.
    """
    import numpy as np
    w, h = img.size
    px = np.asarray(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    filled = px[..., 3] > 0.5
    if filled.all():
        return
    if flat is not None:
        px[~filled] = (*flat, 1.0)
        img.pixels = px.ravel().tolist()
        return
    # low-res weighted fill: downsample to 128, close its gaps by
    # iteration (cheap), upsample back as the fill source
    lw = 128
    while lw > 8 and (h % lw or w % lw):
        lw //= 2
    rgb = px[..., :3]
    blocks = rgb.reshape(lw, h // lw, lw, w // lw).transpose(0, 2, 1, 3)
    mblocks = filled.reshape(lw, h // lw, lw, w // lw).transpose(0, 2, 1, 3)
    cnt = mblocks.sum(axis=(2, 3))
    low = (blocks * mblocks[..., None]).sum(axis=(2, 3)) / np.maximum(cnt, 1)[..., None]
    low_filled = cnt > 0
    for _ in range(2 * lw):
        if low_filled.all():
            break
        acc = np.zeros_like(low)
        n = np.zeros((*low_filled.shape, 1), dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(low * low_filled[..., None], dy, 0), dx, 1)
            sm = np.roll(np.roll(low_filled, dy, 0), dx, 1)
            acc += sh
            n += sm[..., None]
        new = (~low_filled) & (n[..., 0] > 0)
        low[new] = acc[new] / n[new]
        low_filled |= new
    fill = np.repeat(np.repeat(low, h // lw, axis=0), w // lw, axis=1)
    rgb[~filled] = fill[~filled]
    # a few full-res dilation passes so island edges keep their colour
    cur_filled = filled.copy()
    for _ in range(24):
        if cur_filled.all():
            break
        acc = np.zeros_like(rgb)
        n = np.zeros((*cur_filled.shape, 1), dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(rgb * cur_filled[..., None], dy, 0), dx, 1)
            sm = np.roll(np.roll(cur_filled, dy, 0), dx, 1)
            acc += sh
            n += sm[..., None]
        new = (~cur_filled) & (n[..., 0] > 0)
        if not new.any():
            break
        rgb[new] = acc[new] / n[new]
        cur_filled |= new
    px[..., 3] = 1.0
    img.pixels = px.ravel().tolist()


# ------------------------------------------------- edge-preserving reduction

def sharp_edge_sigs(obj, min_angle_deg=30.0):
    """Signatures of the mesh's sharp edges (ridges): (midpoint, unit
    direction) for every edge whose dihedral angle is at least
    min_angle_deg. Boundary edges (one face) count as sharp."""
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.edges.ensure_lookup_table()
    limit = math.radians(min_angle_deg)
    sigs = []
    for e in bm.edges:
        if len(e.link_faces) != 2:
            sharp = True
        else:
            ang = e.calc_face_angle(None)
            sharp = ang is not None and ang >= limit
        if not sharp:
            continue
        a, b = e.verts[0].co, e.verts[1].co
        mid = (a + b) / 2.0
        d = b - a
        if d.length < 1e-9:
            continue
        d.normalize()
        if d.x < 0 or (d.x == 0 and (d.y < 0 or (d.y == 0 and d.z < 0))):
            d = -d
        sigs.append((tuple(round(c, 3) for c in mid), tuple(round(c, 2) for c in d)))
    bm.free()
    return sigs


def sharp_edge_preservation(before, after, tol=0.03):
    """Fraction of `before` sharp edges that still exist in `after`:
    a parallel sharp edge (direction within ~25 deg) whose midpoint is
    within tol metres. This is the numeric ridge-preservation check."""
    import numpy as np
    if not before:
        return 1.0
    if not after:
        return 0.0
    pts = np.array([s[0] for s in after])
    dirs = np.array([s[1] for s in after])
    kept = 0
    for mid, d in before:
        diff = pts - np.array(mid)
        dist = np.sqrt((diff ** 2).sum(axis=1))
        cand = np.nonzero(dist < tol)[0]
        if len(cand) and (np.abs(dirs[cand] @ np.array(d)) > 0.9).any():
            kept += 1
    return kept / len(before)


def reduce_practical(joined, angle_deg=12.0):
    """Edge-preserving low-poly reduction of the joined practical mesh.

    Three mechanical steps, each safe for a building's ridges:
    1. weld coincident vertices (the parts abut exactly by design);
    2. delete coincident interior face pairs -- where two boxes touch,
       their interface faces coincide with opposite normals and are
       strictly inside the solid; removing them also un-blocks step 3
       (they make every neighbouring edge non-manifold);
    3. dissolve only MANIFOLD edges whose two faces are flatter than
       angle_deg and share a material. Corners (90 deg), bevels
       (45 deg), eaves and frame steps are all above the limit and are
       never dissolved -- unlike a naive collapse decimation, which
       happily eats corners. Ridge survival is not just by
       construction: it is measured and returned.
    """
    import bmesh
    from collections import defaultdict
    pre_tris = tri_count(joined)
    pre_sig = sharp_edge_sigs(joined)
    bm = bmesh.new()
    bm.from_mesh(joined.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.0001)
    by_verts = defaultdict(list)
    for f in bm.faces:
        by_verts[tuple(sorted(v.index for v in f.verts))].append(f)
    kill = []
    for fs in by_verts.values():
        if len(fs) == 2 and fs[0].normal.dot(fs[1].normal) < -0.99:
            kill.extend(fs)
    if kill:
        bmesh.ops.delete(bm, geom=kill, context='FACES_ONLY')
    limit = math.radians(angle_deg)
    for _ in range(4):
        bm.edges.ensure_lookup_table()
        targets = []
        for e in bm.edges:
            if len(e.link_faces) != 2:
                continue
            f1, f2 = e.link_faces
            if f1.material_index != f2.material_index:
                continue
            ang = e.calc_face_angle(None)
            if ang is not None and ang < limit:
                targets.append(e)
        if not targets:
            break
        bmesh.ops.dissolve_edges(bm, edges=targets, use_verts=True, use_face_split=False)
    bm.to_mesh(joined.data)
    bm.free()
    joined.data.update()
    post_sig = sharp_edge_sigs(joined)
    return {
        "method": "weld + delete coincident interior face pairs + dissolve manifold edges flatter than the angle limit (same material only); ridges are never dissolved",
        "angle_limit_deg": angle_deg,
        "triangles_before": pre_tris,
        "triangles_after": tri_count(joined),
        "sharp_edges_before": len(pre_sig),
        "sharp_edges_after": len(post_sig),
        "sharp_edge_preservation": round(sharp_edge_preservation(pre_sig, post_sig), 4),
    }


def bake_building(spec, builder, outdir, tex_size):
    """Join the practical parts, bake the high version onto them, export.

    Returns (stats_dict, joined_object).
    """
    parts = builder.parts
    high = builder.make_high()
    stats = {
        "triangles_practical": sum(tri_count(o) for o, _ in parts),
        "triangles_high": sum(tri_count(o) for o in high),
        "parts": len(parts),
        "unique_part_meshes": builder.unique_mesh_count(),
    }
    # Join the high parts into ONE source object before baking: Cycles'
    # selected-to-active bake re-synchronises the whole scene once per
    # selected source object (measured: 12 sources -> 13 full syncs), so
    # ~130 separate parts would cost minutes of pure sync overhead per
    # pass. All parts share the building-local frame, so the join is
    # exact and keeps each part's material per face.
    bpy.ops.object.select_all(action='DESELECT')
    for obj in high:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = high[0]
    bpy.ops.object.join()
    high = [high[0]]
    high[0].name = spec["name"] + "_high"
    # join the practical parts into one object (identity transforms, so
    # the joined geometry stays in building-local coordinates)
    bpy.ops.object.select_all(action='DESELECT')
    for obj, _ in parts:
        obj.select_set(True)
    joined = parts[0][0]
    bpy.context.view_layer.objects.active = joined
    bpy.ops.object.join()
    joined.name = spec["name"]

    # the tool owns the low-poly reduction: edge-preserving dissolve,
    # with the ridge preservation measured and reported
    stats["reduction"] = reduce_practical(joined)
    stats["triangles_practical"] = stats["reduction"]["triangles_after"]

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.005)
    bpy.ops.object.mode_set(mode='OBJECT')

    imgs = {}
    for key, cs in (("albedo", 'sRGB'), ("roughness", 'Non-Color'), ("normal", 'Non-Color')):
        img = bpy.data.images.new(f"{spec['name']}_{key}", tex_size, tex_size)
        img.colorspace_settings.name = cs
        imgs[key] = img

    bake_mat = bpy.data.materials.new(f"{spec['name']}_baketarget")
    bake_mat.use_nodes = True
    target = bake_mat.node_tree.nodes.new("ShaderNodeTexImage")
    joined.data.materials.clear()
    joined.data.materials.append(bake_mat)

    # cage: the target pushed out along its normals (PoC pitfall 1)
    cage = joined.copy()
    cage.data = joined.data.copy()
    cage.name = spec["name"] + "_cage"
    bpy.context.scene.collection.objects.link(cage)
    for v in cage.data.vertices:
        v.co += v.normal * BAKE_CAGE_EXTRUSION

    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = BAKE_SAMPLES
    scene.cycles.device = 'CPU'
    bake = scene.render.bake
    bake.margin = BAKE_MARGIN
    bake.use_selected_to_active = True
    bake.max_ray_distance = BAKE_RAY_DISTANCE
    bake.use_cage = True
    bake.cage_object = cage

    # PoC pitfall 2: Cycles refuses to bake render-hidden objects
    for obj in high:
        obj.hide_render = False
        obj.hide_viewport = False
    joined.hide_render = False
    joined.hide_viewport = False

    bpy.ops.object.select_all(action='DESELECT')
    for obj in high:
        obj.select_set(True)
    joined.select_set(True)
    bpy.context.view_layer.objects.active = joined

    bake.use_pass_direct = False
    bake.use_pass_indirect = False
    bake.use_pass_color = True
    target.image = imgs["albedo"]
    bake_mat.node_tree.nodes.active = target
    bpy.ops.object.bake(type='DIFFUSE')
    target.image = imgs["roughness"]
    bake_mat.node_tree.nodes.active = target
    bpy.ops.object.bake(type='ROUGHNESS')
    target.image = imgs["normal"]
    bake_mat.node_tree.nodes.active = target
    bake.normal_space = 'TANGENT'
    bpy.ops.object.bake(type='NORMAL')

    stats["bake_miss_rate"] = bake_miss_rate(joined, imgs["albedo"])

    # close the gaps between islands before anyone samples this texture
    # (measured above on the raw bake, so the miss rate stays honest)
    fill_gaps(imgs["albedo"])
    fill_gaps(imgs["roughness"], flat=(0.9, 0.9, 0.9))
    fill_gaps(imgs["normal"], flat=(0.5, 0.5, 1.0))

    # Bake high, ship lower: at 1024 the atlas gives a window pane only
    # ~8x15 texels and boundary texels mix in the neighbours (panes read
    # white). Baking at 2048 and downscaling keeps each island's average
    # colour true while the exported set stays 1024.
    if tex_size > EXPORT_TEX_SIZE:
        for img in imgs.values():
            img.scale(EXPORT_TEX_SIZE, EXPORT_TEX_SIZE)
        stats["texture_size"] = EXPORT_TEX_SIZE

    for key, img in imgs.items():
        img.filepath_raw = os.path.join(outdir, f"{spec['name']}_{key}.png")
        img.save()

    # the practical material: one PBR set from the baked images
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
    r = tex_node(imgs["roughness"])
    nt.links.new(r.outputs["Color"], bsdf.inputs["Roughness"])
    n = tex_node(imgs["normal"])
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(n.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    joined.data.materials.clear()
    joined.data.materials.append(mat)

    bpy.data.objects.remove(cage, do_unlink=True)
    bake.use_cage = False

    glb_path = os.path.join(outdir, spec["name"] + ".glb")
    bpy.ops.object.select_all(action='DESELECT')
    joined.select_set(True)
    bpy.context.view_layer.objects.active = joined
    bpy.ops.export_scene.gltf(filepath=glb_path, export_format='GLB',
                              use_selection=True, export_apply=True,
                              export_cameras=False, export_lights=False)
    stats["glb"] = os.path.basename(glb_path)
    stats["glb_bytes"] = os.path.getsize(glb_path)
    stats["bake_texture_size"] = tex_size
    if "texture_size" not in stats:
        stats["texture_size"] = tex_size
    return stats, joined


# ------------------------------------------------------------------ street

def street_layout(buildings):
    """Two rows along a street on X: even indices north (front faces the
    street, no flip), odd indices south (flipped). Deterministic."""
    rows = {"north": [], "south": []}
    for i, b in enumerate(buildings):
        rows["north" if i % 2 == 0 else "south"].append(b)
    layout = []
    for side in ("north", "south"):
        row = rows[side]
        total = sum(b["width"] for b in row) + 3.4 * max(0, len(row) - 1)
        x = -total / 2
        for b in row:
            cx = x + b["width"] / 2
            x += b["width"] + 3.4
            y = (4.9 + b["depth"] / 2) if side == "north" else -(4.9 + b["depth"] / 2)
            layout.append({"name": b["name"], "x": round(cx, 3), "y": round(y, 3),
                           "flip": side == "south"})
    return layout


def setup_street(samples):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.max_bounces = 4
    scene.cycles.diffuse_bounces = 2
    scene.cycles.glossy_bounces = 2
    scene.render.resolution_x, scene.render.resolution_y = RENDER_RES
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

    def plane(name, size, loc, mat):
        bpy.ops.mesh.primitive_plane_add(size=size, location=loc)
        obj = bpy.context.active_object
        obj.name = name
        obj.data.materials.append(mat)
        return obj

    ground = plane("Ground", 70, (0, 0, -0.02), plain_material("GroundMat", (0.13, 0.12, 0.10)))
    ground.scale = (1.0, 0.62, 1.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    road = plane("Road", 1, (0, 0, 0.005), plain_material("RoadMat", (0.055, 0.055, 0.06)))
    road.scale = (23.0, 3.6, 1.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    walk_mat = plain_material("WalkMat", (0.36, 0.35, 0.33))
    for sy in (-1, 1):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, sy * 4.25, 0.06))
        walk = bpy.context.active_object
        walk.name = "Sidewalk"
        walk.dimensions = (46, 1.3, 0.12)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        walk.data.materials.append(walk_mat)

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.lens = 40
    cam = bpy.data.objects.new("Cam", cam_data)
    cam.location = (15.5, -17.5, 7.8)
    cam.rotation_euler = (Vector((0.0, 0.5, 1.4)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.collection.objects.link(cam)
    scene.camera = cam
    return scene


def parent_at(objs, name, entry):
    empty = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(empty)
    empty.location = (entry["x"], entry["y"], 0)
    if entry["flip"]:
        empty.rotation_euler.z = math.pi
    for obj in objs:
        obj.parent = empty
    return empty


def render_to(scene, path):
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print(f"RENDERED {path}")


def frame_camera(scene):
    """Fit the camera to whatever is placed on the street: same view
    direction for every phase, distance scaled to the placed bounds,
    so a 1-building sample and a 5-building street both fill the
    frame comparably."""
    bpy.context.view_layer.update()
    skip = ("Ground", "Road", "Sidewalk")
    mins = Vector((1e9, 1e9, 1e9))
    maxs = Vector((-1e9, -1e9, -1e9))
    found = False
    for obj in bpy.data.objects:
        if obj.type != 'MESH' or obj.name.startswith(skip):
            continue
        found = True
        for corner in obj.bound_box:
            w = obj.matrix_world @ Vector(corner)
            mins = Vector(map(min, mins, w))
            maxs = Vector(map(max, maxs, w))
    if not found:
        return
    center = (mins + maxs) / 2
    span = max(maxs.x - mins.x, maxs.y - mins.y)
    dist = min(60.0, max(24.0, span * 1.35))
    direction = Vector((15.5, -18.0, 6.4)).normalized()
    cam = scene.camera
    cam.location = center + direction * dist
    target = Vector((center.x, center.y, min(1.4, maxs.z * 0.3)))
    cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()


# ------------------------------------------------------- WorldGen-style house

SIMPLE_WALLS = [(0.85, 0.80, 0.72), (0.80, 0.75, 0.68), (0.75, 0.78, 0.80), (0.82, 0.72, 0.66)]
SIMPLE_ROOFS = [(0.45, 0.28, 0.22), (0.30, 0.32, 0.38), (0.38, 0.30, 0.26)]


def build_simple_house(spec, collection):
    """The current UZUWorldGen house, reproduced in Blender from the C++
    recipe (modules/uzu_worldgen): flat box walls with flat colours,
    window quads floating 0.05 in front of the wall, a prism roof.
    No wall thickness, no frames, no foundation, no eaves to speak of."""
    W, D = spec["width"], spec["depth"]
    floors, fh = spec["floors"], spec["floor_height"]
    H = floors * fh
    wall_c = SIMPLE_WALLS[spec["seed"] % len(SIMPLE_WALLS)]
    roof_c = SIMPLE_ROOFS[spec["seed"] % len(SIMPLE_ROOFS)]
    wall_mat = plain_material(f"{spec['name']}_simple_wall", wall_c)
    roof_mat = plain_material(f"{spec['name']}_simple_roof", roof_c)
    win_mat = plain_material(f"{spec['name']}_simple_win", (0.16, 0.20, 0.25), 0.4)
    lit_mat = plain_material(f"{spec['name']}_simple_lit", (1.0, 0.82, 0.5), 0.6)
    parts = []

    def box(name, center, dims, mat):
        bpy.ops.mesh.primitive_cube_add(size=1, location=center)
        obj = bpy.context.active_object
        obj.name = f"{spec['name']}_{name}"
        obj.dimensions = dims
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        obj.data.materials.append(mat)
        for coll in list(obj.users_collection):
            coll.objects.unlink(obj)
        collection.objects.link(obj)
        parts.append(obj)
        return obj

    t = 0.1
    box("wall_f", (0, -D / 2 + t / 2, H / 2), (W, t, H), wall_mat)
    box("wall_b", (0, D / 2 - t / 2, H / 2), (W, t, H), wall_mat)
    box("wall_l", (-W / 2 + t / 2, 0, H / 2), (t, D, H), wall_mat)
    box("wall_r", (W / 2 - t / 2, 0, H / 2), (t, D, H), wall_mat)

    win_i = 0
    for wall in ("front", "back", "left", "right"):
        length = W if wall in ("front", "back") else D
        cols = int((length - 1.0) / 3.0)
        for fl in range(floors):
            for col in range(cols):
                u = -length / 2 + length * (col + 0.5) / cols
                z = fl * fh + fh * 0.5
                mat = lit_mat if win_i % 4 == 3 else win_mat
                win_i += 1
                if wall == "front":
                    box("win", (u, -D / 2 - 0.06, z), (1.2, 0.03, 1.4), mat)
                elif wall == "back":
                    box("win", (u, D / 2 + 0.06, z), (1.2, 0.03, 1.4), mat)
                elif wall == "left":
                    box("win", (-W / 2 - 0.06, u, z), (0.03, 1.2, 1.4), mat)
                else:
                    box("win", (W / 2 + 0.06, u, z), (0.03, 1.2, 1.4), mat)

    if spec["roof"] == "flat":
        box("roof", (0, 0, H + 0.04), (W + 0.1, D + 0.1, 0.08), roof_mat)
    else:
        short = min(W, D)
        rise = short * 0.38
        long_is_x = W >= D
        span = D if long_is_x else W
        half = span / 2 + 0.05
        theta = math.atan2(rise, half)
        slope = math.hypot(half, rise)
        if long_is_x:
            for sgn, rot in ((-1, theta), (1, -theta)):
                bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0))
                obj = bpy.context.active_object
                obj.name = f"{spec['name']}_roof"
                obj.dimensions = (W + 0.1, slope, 0.06)
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
                m = Matrix.Translation((0, sgn * half / 2, H + rise / 2)) @ Matrix.Rotation(rot, 4, 'X')
                obj.data.transform(m)
                obj.data.materials.append(roof_mat)
                for coll in list(obj.users_collection):
                    coll.objects.unlink(obj)
                collection.objects.link(obj)
                parts.append(obj)
            for sx in (-1, 1):
                cx = sx * (W / 2 - 0.03)
                prof = [(-D / 2, H), (D / 2, H), (0.0, H + rise)]
                verts = [(cx - 0.03, y, z) for (y, z) in prof] + [(cx + 0.03, y, z) for (y, z) in prof]
                faces = [(2, 1, 0), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]
                mesh = bpy.data.meshes.new(f"{spec['name']}_gable")
                mesh.from_pydata(verts, [], faces)
                mesh.update()
                obj = bpy.data.objects.new(f"{spec['name']}_gable", mesh)
                collection.objects.link(obj)
                mesh.materials.append(wall_mat)
                parts.append(obj)
        else:
            for sgn, rot in ((-1, theta), (1, -theta)):
                bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0))
                obj = bpy.context.active_object
                obj.name = f"{spec['name']}_roof"
                obj.dimensions = (D + 0.1, slope, 0.06)
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
                m = Matrix.Rotation(math.radians(90), 4, 'Z') @ Matrix.Translation((0, sgn * half / 2, H + rise / 2)) @ Matrix.Rotation(rot, 4, 'X')
                obj.data.transform(m)
                obj.data.materials.append(roof_mat)
                for coll in list(obj.users_collection):
                    coll.objects.unlink(obj)
                collection.objects.link(obj)
                parts.append(obj)
            for sy in (-1, 1):
                cy = sy * (D / 2 - 0.03)
                prof = [(-W / 2, H), (W / 2, H), (0.0, H + rise)]
                verts = [(x, cy - 0.03, z) for (x, z) in prof] + [(x, cy + 0.03, z) for (x, z) in prof]
                faces = [(2, 1, 0), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]
                mesh = bpy.data.meshes.new(f"{spec['name']}_gable")
                mesh.from_pydata(verts, [], faces)
                mesh.update()
                obj = bpy.data.objects.new(f"{spec['name']}_gable", mesh)
                collection.objects.link(obj)
                mesh.materials.append(wall_mat)
                parts.append(obj)
    return parts


# ------------------------------------------------------------------ run

def run(spec, outdir):
    os.makedirs(outdir, exist_ok=True)
    tex_size = int(spec.get("texture_size", 2048))
    samples = int(spec.get("render_samples", 48))
    buildings = spec["buildings"]
    stats = {"buildings": [], "renders": {}}
    by_name = {b["name"]: b for b in buildings}

    # ---- stage 1: build + bake + export each building at the origin
    clean_scene()
    for b in buildings:
        coll = bpy.data.collections.new("B_" + b["name"])
        bpy.context.scene.collection.children.link(coll)
        coords = bpy.data.objects.new("Coords_" + b["name"], None)
        coll.objects.link(coords)
        builder = Builder(b, coll, coords)
        builder.build()
        bstats, _joined = bake_building(b, builder, outdir, tex_size)
        entry = {"name": b["name"]}
        entry.update(bstats)
        stats["buildings"].append(entry)
        print(f"BUILT {b['name']}: {json.dumps(entry)}")

    layout = street_layout(buildings)
    stats["layout"] = layout
    at = {e["name"]: e for e in layout}

    # ---- stage 2 (a): WorldGen-style houses on the same street
    clean_scene()
    scene = setup_street(samples)
    for b in buildings:
        coll = bpy.data.collections.new("S_" + b["name"])
        scene.collection.children.link(coll)
        parts = build_simple_house(b, coll)
        parent_at(parts, "Simple_" + b["name"], at[b["name"]])
    frame_camera(scene)
    path = os.path.join(outdir, "render_a_worldgen.png")
    render_to(scene, path)
    stats["renders"]["a_worldgen"] = os.path.basename(path)

    # ---- stage 3 (b): recipe buildings, high density, plain grey
    clean_scene()
    scene = setup_street(samples)
    grey = gray_material()
    for b in buildings:
        coll = bpy.data.collections.new("H_" + b["name"])
        scene.collection.children.link(coll)
        coords = bpy.data.objects.new("Coords_" + b["name"], None)
        coll.objects.link(coords)
        builder = Builder(b, coll, coords)
        builder.build()
        high = builder.make_high()
        for obj, _detail in builder.parts:
            bpy.data.objects.remove(obj, do_unlink=True)
        for obj in high:
            obj.data.materials.clear()
            obj.data.materials.append(grey)
        parent_at(high, "High_" + b["name"], at[b["name"]])
    frame_camera(scene)
    path = os.path.join(outdir, "render_b_recipe_gray.png")
    render_to(scene, path)
    stats["renders"]["b_recipe_gray"] = os.path.basename(path)

    # ---- stage 4 (c): the exported practical GLBs, re-imported
    clean_scene()
    scene = setup_street(samples)
    for b in buildings:
        glb = os.path.join(outdir, b["name"] + ".glb")
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=glb)
        imported = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
        entry = at[b["name"]]
        for obj in imported:
            obj.location = (entry["x"], entry["y"], 0)
            if entry["flip"]:
                obj.rotation_euler.z = math.pi
    frame_camera(scene)
    path = os.path.join(outdir, "render_c_recipe_textured.png")
    render_to(scene, path)
    stats["renders"]["c_recipe_textured"] = os.path.basename(path)

    stats_path = os.path.join(outdir, "stats.json")
    with open(stats_path, "w", encoding="utf-8") as fh:
        json.dump(stats, fh, indent=2, ensure_ascii=False)
    print("UZU_BUILDING_STATS " + stats_path)
    return stats
