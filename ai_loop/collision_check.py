#!/usr/bin/env python3
"""Mechanical collision check of a saved scene (BACKLOG B-1224): the
analysis half.

The engine side (ai_loop/collision_tool.gd) re-opens a saved .tscn/.scn
and extracts raw geometry -- collision solids (world AABBs, heightfield
data, concave faces, boundary planes), visual footprints (mesh and
MultiMesh instance AABBs) and the uzu_places metadata -- without
interpreting any of it. This module rasterizes that geometry onto an
XZ grid and checks it mechanically:

  (1) visual/collision agreement: for every sizable mesh, the share of
      its footprint that collision actually covers (and, per solid,
      the share of the collision footprint no visual explains --
      overhang / invisible walls). A walkable cell a visual blocks on
      screen is a "hole"; a hole cluster bridging two regions that the
      visuals keep apart is a wall leak (すり抜け).
  (2) walkable connectivity: each cell gets a stand height (the
      highest surface with body-height headroom) and a directed flood
      fill from the start points applies the step rule -- rising more
      than --step across a surface change is impossible, falling is
      free, and along one continuous surface the slope is capped.
      Places no start can reach, starts that cannot reach each other
      (with the bottleneck: the cheapest crossing and its required
      rise), pockets (walkable regions with no start and no place),
      and start pairs whose STRAIGHT approach is walled by an
      over-step ledge even when a detour exists (direct_route_blocked)
      are reported with coordinates.
  (3) gaps: phantom collision (blocked cells no visual explains) that
      splits a region the visuals keep whole is a plugged gap (塞ぎ);
      plain phantom clusters are reported as invisible obstacles.
  (4) overlap: collision AABBs of different bodies that intersect by
      volume -- double-defined collision.

Everything is a static, geometric approximation of what a physics
walker (B-1510) proves dynamically: AABB footprints stand in for
rotated shapes, the grid resolution is the character's width, and
multi-storey interiors collapse to their highest standable surface.
Findings carry severity: "error" fails the check (ok=false),
"warning" and "info" do not.

uzu.py collision-check is the entry point; this module is also a small
CLI for tests: collision_check.py --extract extract.json [--out
report.json] [--heatmap map.png] [--start x,z ...]. Standard library
only.
"""

import json
import math
import os
import struct
import sys
import zlib
from collections import deque

REPORT_FORMAT = "uzu-collision-check/1"

DEFAULTS = {
    "cell": 0.5,          # grid resolution (m); ~ the character's width
    "step": 0.25,         # max rise across a surface change (m)
    "char_width": 0.7,    # the walker's width (m); body-fit inflation
    "body_height": 1.8,   # headroom a stand surface needs (m)
    "max_slope": 55.0,    # degrees; along one continuous surface
    "min_visual_area": 1.0,   # m^2; smaller meshes skip the coverage check
    "coverage_min": 0.8,      # covered share a sizable visual needs
    "overhang_max": 0.25,     # unexplained share a solid may have
    "min_pocket_area": 5.0,   # m^2; smaller pockets are not reported
}

MAX_CELLS = 9_000_000


# ------------------------------------------------------------------ helpers


def _aabb_xz(a):
    return a[0], a[2], a[3], a[5]


def _cluster_cells(cells, width):
    """Connected clusters (4-neighbourhood) of a set of flat cell ids."""
    seen = set()
    clusters = []
    for c in cells:
        if c in seen:
            continue
        comp = []
        queue = deque([c])
        seen.add(c)
        while queue:
            cur = queue.popleft()
            comp.append(cur)
            x, z = cur % width, cur // width
            for nb in (cur - 1 if x > 0 else -1, cur + 1 if x < width - 1 else -1,
                       cur - width if z > 0 else -1, cur + width if z < width - 1 else -1):
                if nb >= 0 and nb in cells and nb not in seen:
                    seen.add(nb)
                    queue.append(nb)
        clusters.append(comp)
    return clusters


class Grid:
    """The rasterized scene: per-cell stand height/source, the layers
    the checks read, and the movement rules between cells."""

    def __init__(self, extract, opts):
        self.opts = opts
        self.cell = float(opts["cell"])
        solids = extract.get("solids") or []
        self.aabb_solids = []      # {path, body_key, aabb}
        self.heightmaps = []       # {path, sample(x, z) -> h|None, extent}
        self.tri_solids = []       # {path, faces}
        self.planes = []           # {path, normal, d}
        self.solid_ids = {}        # extraction index -> grid solid id
        for idx, s in enumerate(solids):
            kind = s.get("kind")
            if kind in ("box", "sphere", "capsule", "cylinder", "convex"):
                self.solid_ids[idx] = len(self.aabb_solids)
                self.aabb_solids.append({
                    "path": s["path"], "aabb": s["aabb"],
                    "body_key": s["path"].rsplit("/", 1)[0] if "/" in s["path"] else "",
                })
            elif kind == "heightmap":
                self.solid_ids[idx] = 100000 + len(self.heightmaps)
                self.heightmaps.append(_Heightmap(s))
            elif kind == "tris":
                self.solid_ids[idx] = 200000 + len(self.tri_solids)
                self.tri_solids.append(s)
            elif kind == "plane":
                self.planes.append(s)
        self.visuals = extract.get("visuals") or []
        self.multimeshes = extract.get("multimeshes") or []
        self.places = extract.get("places") or []
        self._bounds(extract)
        self._rasterize()

    # ------------------------------------------------------------ bounds

    def _bounds(self, extract):
        xs, zs = [], []

        def eat(a):
            xs.extend((a[0], a[3]))
            zs.extend((a[2], a[5]))

        for s in self.aabb_solids:
            eat(s["aabb"])
        for hm in self.heightmaps:
            eat(hm.extent_aabb)
        for s in self.tri_solids:
            f = s["faces"]
            if f:
                eat([min(f[0::3]), 0.0, min(f[2::3]), max(f[0::3]), 0.0, max(f[2::3])])
        for v in self.visuals:
            eat(v["aabb"])
        for mm in self.multimeshes:
            for a in mm["instances"]:
                eat(a)
        for p in self.places:
            if len(p.get("position") or []) >= 3:
                xs.append(p["position"][0])
                zs.append(p["position"][2])
        if not xs:
            raise ValueError("the scene has no geometry to check")
        pad = 2 * self.cell
        self.minx, self.maxx = min(xs) - pad, max(xs) + pad
        self.minz, self.maxz = min(zs) - pad, max(zs) + pad
        self.width = int(math.ceil((self.maxx - self.minx) / self.cell))
        self.height = int(math.ceil((self.maxz - self.minz) / self.cell))
        if self.width * self.height > MAX_CELLS:
            raise ValueError(
                f"grid would be {self.width}x{self.height} cells; raise --cell "
                f"(scene spans {self.maxx - self.minx:.0f}x{self.maxz - self.minz:.0f} m)")

    def cell_at(self, x, z):
        i = int((x - self.minx) // self.cell)
        j = int((z - self.minz) // self.cell)
        if 0 <= i < self.width and 0 <= j < self.height:
            return j * self.width + i
        return None

    def cell_center(self, c):
        return (self.minx + (c % self.width + 0.5) * self.cell,
                self.minz + (c // self.width + 0.5) * self.cell)

    def _cell_range(self, minx, minz, maxx, maxz):
        i0 = max(0, int((minx - self.minx) // self.cell))
            # cells whose centre falls in the range, widened by one
        i1 = min(self.width - 1, int((maxx - self.minx) // self.cell))
        j0 = max(0, int((minz - self.minz) // self.cell))
        j1 = min(self.height - 1, int((maxz - self.minz) // self.cell))
        return i0, i1, j0, j1

    # --------------------------------------------------------- rasterize

    def _rasterize(self):
        n = self.width * self.height
        nan = float("nan")
        self.hfield = [nan] * n        # highest heightfield surface per cell
        self.hfield_id = [-1] * n
        self.intervals = {}            # cell -> [(lo, hi, solid_id)]
        self.tri_tops = {}             # cell -> [(height, solid_id)]
        for hm_i, hm in enumerate(self.heightmaps):
            sid = 100000 + hm_i
            i0, i1, j0, j1 = self._cell_range(*_aabb_xz(hm.extent_aabb))
            for j in range(j0, j1 + 1):
                for i in range(i0, i1 + 1):
                    x, z = self.cell_center(j * self.width + i)
                    h = hm.sample(x, z)
                    if h is not None:
                        c = j * self.width + i
                        if math.isnan(self.hfield[c]) or h > self.hfield[c]:
                            self.hfield[c] = h
                            self.hfield_id[c] = sid
        for s_i, s in enumerate(self.aabb_solids):
            a = s["aabb"]
            i0, i1, j0, j1 = self._cell_range(a[0], a[2], a[3], a[5])
            for j in range(j0, j1 + 1):
                for i in range(i0, i1 + 1):
                    # Intersection, not centre sampling: a wall thinner
                    # than one cell still blocks the cells it crosses.
                    self.intervals.setdefault(j * self.width + i, []).append((a[1], a[4], s_i))
        for t_i, s in enumerate(self.tri_solids):
            sid = 200000 + t_i
            f = s["faces"]
            for k in range(0, len(f) - 8, 9):
                tri = f[k:k + 9]
                nx, ny, nz = _tri_normal(tri)
                if ny < 0.5:
                    continue  # only up-facing faces are standable
                i0, i1, j0, j1 = self._cell_range(
                    min(tri[0], tri[3], tri[6]), min(tri[2], tri[5], tri[8]),
                    max(tri[0], tri[3], tri[6]), max(tri[2], tri[5], tri[8]))
                for j in range(j0, j1 + 1):
                    for i in range(i0, i1 + 1):
                        x, z = self.cell_center(j * self.width + i)
                        h = _tri_height(tri, x, z)
                        if h is not None:
                            self.tri_tops.setdefault(j * self.width + i, []).append((h, sid))
        # Stand heights: the highest surface with body-height headroom.
        body_h = float(self.opts["body_height"])
        self.stand = [nan] * n
        self.stand_source = [-1] * n
        self.floor_ref = [nan] * n     # the lowest surface in the cell
        self.blockers = {}             # blocked cell -> [solid_id]
        for c in range(n):
            tops = []
            ivs = self.intervals.get(c) or []
            for lo, hi, sid in ivs:
                tops.append((hi, sid))
            for h, sid in self.tri_tops.get(c) or []:
                tops.append((h, sid))
            if not math.isnan(self.hfield[c]):
                tops.append((self.hfield[c], self.hfield_id[c]))
            for pl in self.planes:
                h = _plane_height(pl, *self.cell_center(c))
                if h is not None:
                    tops.append((h, 300000))
            if not tops:
                continue
            self.floor_ref[c] = min(t for t, _ in tops)
            tops.sort(reverse=True)
            for top, sid in tops:
                if not any(lo < top + body_h and hi > top for lo, hi, _ in ivs):
                    self.stand[c] = top
                    self.stand_source[c] = sid
                    break
            else:
                top = tops[0][0]
                self.blockers[c] = [sid for lo, hi, sid in ivs
                                    if lo < top + body_h and hi > top]
        self._body_fit()

    def _body_fit(self):
        """Traversable cells: a stand surface where the walker's body
        (char_width across, body_height tall) also fits sideways. A
        neighbouring interval obstructs only where it reaches into the
        body band ABOVE step height: a curb or a raised floor next door
        (at or below the step limit) does not block standing, a wall
        does. The grid therefore keeps passages of roughly
        char_width + cell open and closes narrower ones."""
        body_h = float(self.opts["body_height"])
        step = float(self.opts["step"])
        r = int(math.ceil((float(self.opts["char_width"]) * 0.5) / self.cell))
        n = self.width * self.height
        self.traversable = bytearray(n)
        offsets = [(di, dj) for dj in range(-r, r + 1) for di in range(-r, r + 1)
                   if di * di + dj * dj <= r * r]
        for c in range(n):
            top = self.stand[c]
            if math.isnan(top):
                continue
            i, j = c % self.width, c // self.width
            fits = True
            for di, dj in offsets:
                ni, nj = i + di, j + dj
                if not (0 <= ni < self.width and 0 <= nj < self.height):
                    continue
                for lo, hi, _ in self.intervals.get(nj * self.width + ni) or []:
                    if lo < top + body_h and hi > top + step:
                        fits = False
                        break
                if not fits:
                    break
            self.traversable[c] = 1 if fits else 0

    # ------------------------------------------------------------ movement

    def move_allowed(self, a, b):
        """Directed step a -> b between adjacent traversable cells."""
        if not self.traversable[b]:
            return False
        sa, sb = self.stand[a], self.stand[b]
        if self.stand_source[a] == self.stand_source[b] and self.stand_source[a] >= 0:
            limit = self.cell * math.tan(math.radians(float(self.opts["max_slope"])))
            return abs(sb - sa) <= limit + 1e-9
        return sb - sa <= float(self.opts["step"]) + 1e-9

    def neighbors(self, c):
        i, j = c % self.width, c // self.width
        if i > 0:
            yield c - 1
        if i < self.width - 1:
            yield c + 1
        if j > 0:
            yield c - self.width
        if j < self.height - 1:
            yield c + self.width

    def flood(self, start):
        """The set of cells reachable from start under the step rule."""
        seen = {start}
        queue = deque([start])
        while queue:
            cur = queue.popleft()
            for nb in self.neighbors(cur):
                if nb not in seen and self.traversable[nb] and self.move_allowed(cur, nb):
                    seen.add(nb)
                    queue.append(nb)
        return seen

    def components(self):
        """Undirected components of traversable cells. An edge needs
        the step rule to pass BOTH ways, so a one-way drop (a ledge, a
        roof) does not merge two regions into one component."""
        comp = [-1] * (self.width * self.height)
        groups = []
        for c in range(self.width * self.height):
            if not self.traversable[c] or comp[c] >= 0:
                continue
            gid = len(groups)
            cells = []
            queue = deque([c])
            comp[c] = gid
            while queue:
                cur = queue.popleft()
                cells.append(cur)
                for nb in self.neighbors(cur):
                    if comp[nb] < 0 and self.traversable[nb] and (
                            self.move_allowed(cur, nb) and self.move_allowed(nb, cur)):
                        comp[nb] = gid
                        queue.append(nb)
            groups.append(cells)
        return comp, groups

    def snap(self, x, z, max_dist=3.0):
        """The nearest traversable cell to (x, z), or None."""
        c0 = self.cell_at(x, z)
        if c0 is None:
            return None
        rings = int(max_dist / self.cell) + 1
        i0, j0 = c0 % self.width, c0 // self.width
        for ring in range(rings + 1):
            best, best_d = None, None
            for dj in range(-ring, ring + 1):
                for di in range(-ring, ring + 1):
                    if max(abs(di), abs(dj)) != ring:
                        continue
                    ni, nj = i0 + di, j0 + dj
                    if 0 <= ni < self.width and 0 <= nj < self.height:
                        c = nj * self.width + ni
                        if self.traversable[c]:
                            d = di * di + dj * dj
                            if best_d is None or d < best_d:
                                best, best_d = c, d
            if best is not None:
                return best
        return None


class _Heightmap:
    """A HeightMapShape3D solid: bilinear sampling of its data through
    the inverse of its world transform (exact for the usual case of a
    translated / Y-rotated / uniformly scaled field)."""

    def __init__(self, s):
        self.path = s["path"]
        self.width = int(s["map_width"])
        self.depth = int(s["map_depth"])
        self.data = s["data"]
        o = s["origin"]
        b = s["basis"]  # columns: [x.x x.y x.z, y.x y.y y.z, z.x z.y z.z]
        self.origin = o
        self.basis = b
        m = [[b[0], b[3], b[6]], [b[1], b[4], b[7]], [b[2], b[5], b[8]]]
        self.inv = _invert3(m)
        hw, hd = (self.width - 1) / 2.0, (self.depth - 1) / 2.0
        corners = []
        for lx in (-hw, hw):
            for lz in (-hd, hd):
                corners.append((o[0] + b[0] * lx + b[6] * lz,
                                o[2] + b[2] * lx + b[8] * lz))
        xs = [c[0] for c in corners]
        zs = [c[1] for c in corners]
        self.extent_aabb = [min(xs), 0.0, min(zs), max(xs), 0.0, max(zs)]

    def sample(self, x, z):
        if self.inv is None:
            return None
        dx, dz = x - self.origin[0], z - self.origin[2]
        inv = self.inv
        lx = inv[0][0] * dx + inv[0][2] * dz
        lz = inv[2][0] * dx + inv[2][2] * dz
        fx = lx + (self.width - 1) / 2.0
        fz = lz + (self.depth - 1) / 2.0
        if fx < 0 or fz < 0 or fx > self.width - 1 or fz > self.depth - 1:
            return None
        i0, j0 = int(math.floor(fx)), int(math.floor(fz))
        i1, j1 = min(i0 + 1, self.width - 1), min(j0 + 1, self.depth - 1)
        tx, tz = fx - i0, fz - j0
        d, w = self.data, self.width
        h = (d[j0 * w + i0] * (1 - tx) * (1 - tz) + d[j0 * w + i1] * tx * (1 - tz)
             + d[j1 * w + i0] * (1 - tx) * tz + d[j1 * w + i1] * tx * tz)
        b = self.basis
        return self.origin[1] + b[1] * lx + b[4] * h + b[7] * lz


def _invert3(m):
    a, b, c = m[0]
    d, e, f = m[1]
    g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1e-12:
        return None
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def _tri_normal(t):
    ux, uy, uz = t[3] - t[0], t[4] - t[1], t[5] - t[2]
    vx, vy, vz = t[6] - t[0], t[7] - t[1], t[8] - t[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length < 1e-12:
        return 0.0, 0.0, 0.0
    return nx / length, ny / length, nz / length


def _tri_height(t, x, z):
    """The height of triangle t above (x, z), or None outside it."""
    x0, z0, x1, z1, x2, z2 = t[0], t[2], t[3], t[5], t[6], t[8]
    den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
    if abs(den) < 1e-12:
        return None
    w0 = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den
    w1 = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den
    w2 = 1.0 - w0 - w1
    if w0 < -1e-9 or w1 < -1e-9 or w2 < -1e-9:
        return None
    return w0 * t[1] + w1 * t[4] + w2 * t[7]


def _plane_height(pl, x, z):
    n = pl["normal"]
    if abs(n[1]) < 0.5:
        return None
    return -(n[0] * x + n[2] * z + pl["d"]) / n[1]


# ------------------------------------------------------------------- checks


def _round3(v):
    return [round(float(v[0]), 2), round(float(v[1]), 2), round(float(v[2]), 2)]


def _finding(kind, severity, position, detail, objects=None, **evidence):
    f = {"kind": kind, "severity": severity, "position": position,
         "detail": detail, "objects": objects or []}
    if evidence:
        f["evidence"] = evidence
    return f


class _Visuals:
    """Rasterized visual layers. MeshInstance3D geometry is rasterized
    from its world-space triangles (a merged mesh's AABB spans far
    more than its geometry); MultiMesh instances and meshes without
    readable triangles fall back to AABBs. solid_cells feeds the
    hole/phantom tests, all_cells the overhang explanation, and
    mesh_cells / mesh_tri_cells the per-mesh coverage check."""

    def __init__(self, grid):
        self.solid_cells = {}   # cell -> [(lo, hi, path)]
        self.all_cells = {}     # cell -> [(lo, hi, path)]
        self.mesh_cells = []    # per visuals[] entry: [cells] or None
        self.mesh_tri_cells = []  # per entry: {cell: (lo, hi)} or None
        for mm in grid.multimeshes:
            for a in mm["instances"]:
                if (a[3] - a[0]) * (a[5] - a[2]) < 0.05:
                    continue
                i0, i1, j0, j1 = grid._cell_range(a[0], a[2], a[3], a[5])
                for j in range(j0, j1 + 1):
                    for i in range(i0, i1 + 1):
                        self.all_cells.setdefault(j * grid.width + i, []).append(
                            (a[1], a[4], mm["path"]))
        for idx, v in enumerate(grid.visuals):
            a = v["aabb"]
            area = (a[3] - a[0]) * (a[5] - a[2])
            height = a[4] - a[1]
            name = v["path"].rsplit("/", 1)[-1]
            solid = area >= 0.2 and height >= 0.4 and name != "Water"
            tri_cells = None
            if v.get("tris"):
                tri_cells = {}
                self._raster_tris(grid, v, tri_cells)
                for c, (lo, hi) in tri_cells.items():
                    self.all_cells.setdefault(c, []).append((lo, hi, v["path"]))
                    if solid:
                        self.solid_cells.setdefault(c, []).append((lo, hi, v["path"]))
            else:
                if area >= 0.05:
                    i0, i1, j0, j1 = grid._cell_range(a[0], a[2], a[3], a[5])
                    for j in range(j0, j1 + 1):
                        for i in range(i0, i1 + 1):
                            c = j * grid.width + i
                            self.all_cells.setdefault(c, []).append((a[1], a[4], v["path"]))
                            if solid:
                                self.solid_cells.setdefault(c, []).append((a[1], a[4], v["path"]))
            # Coverage eligibility: sizable, upright meshes. Terrain is
            # covered by its heightfield by construction (not an AABB
            # solid), so it is excluded by name, as in the audit.
            cells = None
            if name != "Terrain" and height >= 0.5:
                if tri_cells is not None:
                    if len(tri_cells) * grid.cell * grid.cell >= float(grid.opts["min_visual_area"]):
                        cells = list(tri_cells)
                elif area >= float(grid.opts["min_visual_area"]):
                    i0, i1, j0, j1 = grid._cell_range(a[0], a[2], a[3], a[5])
                    cells = [j * grid.width + i
                             for j in range(j0, j1 + 1) for i in range(i0, i1 + 1)]
            self.mesh_cells.append(cells)
            self.mesh_tri_cells.append(tri_cells)

    def _raster_tris(self, grid, v, out):
        """Per-cell (lo, hi) visual intervals from one mesh's triangles.
        Floor-ish triangles contribute their plane height at the cell
        centre (+-0.2 m); steep triangles (walls, pitched roofs) their
        full y span, and claim every cell their XZ projection passes
        within 3/4 of a cell of -- a wall projects to a line in XZ, so
        centre sampling alone would miss it."""
        tris = v["tris"]
        reach = 0.75 * grid.cell
        for k in range(0, len(tris) - 8, 9):
            t = tris[k:k + 9]
            _, ny, _ = _tri_normal(t)
            i0, i1, j0, j1 = grid._cell_range(
                min(t[0], t[3], t[6]) - reach, min(t[2], t[5], t[8]) - reach,
                max(t[0], t[3], t[6]) + reach, max(t[2], t[5], t[8]) + reach)
            steep = abs(ny) < 0.7
            for j in range(j0, j1 + 1):
                for i in range(i0, i1 + 1):
                    x, z = grid.cell_center(j * grid.width + i)
                    if steep:
                        if _point_tri_dist2d(t, x, z) > reach:
                            continue
                        lo = min(t[1], t[4], t[7])
                        hi = max(t[1], t[4], t[7])
                    else:
                        h = _tri_height(t, x, z)
                        if h is None:
                            continue
                        lo, hi = h - 0.2, h + 0.2
                    c = j * grid.width + i
                    if c in out:
                        plo, phi = out[c]
                        out[c] = (min(plo, lo), max(phi, hi))
                    else:
                        out[c] = (lo, hi)


def _point_tri_dist2d(t, x, z):
    """Distance from (x, z) to the triangle's XZ projection (0 inside)."""
    pts = [(t[0], t[2]), (t[3], t[5]), (t[6], t[8])]
    if _tri_height(t, x, z) is not None:
        return 0.0
    best = float("inf")
    for m in range(3):
        ax, az = pts[m]
        bx, bz = pts[(m + 1) % 3]
        dx, dz = bx - ax, bz - az
        denom = dx * dx + dz * dz
        u = 0.0 if denom < 1e-12 else max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / denom))
        px, pz = ax + u * dx, az + u * dz
        best = min(best, math.hypot(x - px, z - pz))
    return best


def _overlaps(lo1, hi1, lo2, hi2):
    return min(hi1, hi2) - max(lo1, lo2)


def analyze(extract, opts=None, starts=None):
    """(report, raster) for one extraction. starts: [(x, z, label)];
    default starts are the places whose id names a spawn, else the
    centroid of the largest walkable component."""
    opts = {**DEFAULTS, **(opts or {})}
    grid = Grid(extract, opts)
    vis = _Visuals(grid)
    body_h = float(opts["body_height"])
    cell_area = grid.cell * grid.cell
    findings = []

    # ---- phantom collision ------------------------------------------
    # At floor level: an interval reaching into the body band above the
    # cell's lowest surface (floor_ref) that no visual explains, in a
    # cell the walker cannot occupy at that level (no stand at all, or
    # the only stand is on top of the obstruction itself, past the
    # step limit).
    phantom = set()
    step = float(opts["step"])
    for c in range(grid.width * grid.height):
        ref = grid.floor_ref[c]
        if math.isnan(ref):
            continue
        stand = grid.stand[c]
        if not math.isnan(stand) and stand <= ref + step:
            continue  # the walker occupies this cell at floor level
        for lo, hi, sid in grid.intervals.get(c) or []:
            if not (lo < ref + body_h and hi > ref):
                continue
            explained = any(
                _overlaps(vlo, vhi, lo, hi) > 0.05
                for vlo, vhi, _ in vis.all_cells.get(c) or [])
            if not explained:
                phantom.add(c)
                break

    # ---- connectivity ----------------------------------------------
    comp, groups = grid.components()
    place_cells = {}
    for p in grid.places:
        pos = p.get("position") or []
        if len(pos) >= 3:
            place_cells[p["id"]] = grid.snap(pos[0], pos[2])
    start_points = []
    if starts:
        start_points = [(x, z, label) for x, z, label in starts]
    else:
        for p in grid.places:
            pos = p.get("position") or []
            if "spawn" in p["id"].lower() and len(pos) >= 3:
                start_points.append((pos[0], pos[2], p["id"]))
        if not start_points and groups:
            biggest = max(groups, key=len)
            cx = sum(grid.cell_center(c)[0] for c in biggest) / len(biggest)
            cz = sum(grid.cell_center(c)[1] for c in biggest) / len(biggest)
            start_points = [(cx, cz, "largest_component")]
    fills = {}
    start_report = []
    for x, z, label in start_points:
        c = grid.snap(x, z)
        if c is None:
            findings.append(_finding(
                "start_not_walkable", "error", [x, 0.0, z],
                f"start '{label}' has no traversable cell within 3 m"))
            continue
        fills[label] = grid.flood(c)
        sx, sz = grid.cell_center(c)
        start_report.append({"label": label, "position": _round3([sx, grid.stand[c], sz]),
                             "cells_reached": len(fills[label])})
    reached_any = set()
    for s in fills.values():
        reached_any |= s
    reach_field = _reach_field(grid, reached_any)

    # ---- holes: visual geometry a walker can enter ------------------
    # Only cells a start actually reaches are judged: a visual over an
    # unreachable surface (a pitched roof over a flat collision top)
    # clips a walker who can never stand there, which is not a defect
    # of the walkable world. The band overlap must exceed 0.3 m, so
    # paper-thin visuals (water, decals, the terrain skin itself) do
    # not count.
    hole = set()
    for c in reached_any:
        top = grid.stand[c]
        for lo, hi, _ in vis.solid_cells.get(c) or []:
            if _overlaps(lo, hi, top, top + body_h) > 0.3:
                hole.add(c)
                break

    # Direct approaches between starts: the straight line is what a
    # player tries first; where it is walled by a ledge over the step
    # limit, that is reported even when a detour exists (the static
    # counterpart of a walker failing the direct leg).
    start_cells = {}
    for x, z, label in start_points:
        c = grid.snap(x, z)
        if c is not None:
            start_cells[label] = (c, x, z)
    labels = list(fills)
    for a_i, a in enumerate(labels):
        for b in labels[a_i + 1:]:
            if a not in start_cells or b not in start_cells:
                continue
            ca, cb = start_cells[a][0], start_cells[b][0]
            ax, az = grid.cell_center(ca)
            bx, bz = grid.cell_center(cb)
            dist = math.hypot(bx - ax, bz - az)
            if dist < 5.0:
                continue
            blocked = _line_block(grid, ca, cb)
            if blocked is not None:
                findings.append(_finding(
                    "direct_route_blocked", "info", blocked["at"],
                    f"the straight approach from start '{a}' to start '{b}' is blocked "
                    f"({blocked['why']}); the starts are "
                    + ("connected by a detour" if cb in fills[a] and ca in fills[b]
                       else "not mutually reachable"),
                    **{"from": a, "to": b, "required_rise_m": blocked["rise"],
                       "straight_m": round(dist, 1)}))

    places_report = []
    for p in grid.places:
        pos = p.get("position") or []
        if len(pos) < 3:
            continue
        c = place_cells.get(p["id"])
        entry = {"id": p["id"], "position": _round3(pos),
                 "stand": None if c is None else round(grid.stand[c], 2),
                 "reached_from": [label for label, s in fills.items() if c in s],
                 "component": None if c is None else comp[c]}
        places_report.append(entry)
        if c is None:
            findings.append(_finding(
                "place_not_walkable", "error", _round3(pos),
                f"place '{p['id']}' has no traversable cell within 3 m"))
        elif not entry["reached_from"]:
            src = reach_field[c]
            elevated = not math.isnan(src) and grid.stand[c] - src > body_h
            findings.append(_finding(
                "place_unreachable", "info" if elevated else "error", _round3(pos),
                f"place '{p['id']}' is not reached from any start"
                + ("; it sits on an elevated surface (reachable only by falling onto it)"
                   if elevated else ""),
                stand=round(grid.stand[c], 2), component=comp[c]))
    labels = list(fills)
    for a_i, a in enumerate(labels):
        for b in labels[a_i + 1:]:
            b_cells = [place_cells[p["id"]] for p in grid.places
                       if p["id"] == b and place_cells.get(p["id"]) is not None]
            target = b_cells[0] if b_cells else None
            if target is None:
                continue
            if target not in fills[a]:
                findings.append(_finding(
                    "starts_disconnected", "error", None,
                    f"start '{b}' cannot be reached from start '{a}'",
                    **_bottleneck(grid, fills[a], comp[target], target)))

    comp_of_start = set()
    for label, s in fills.items():
        for c in list(s)[:1]:
            comp_of_start.add(comp[c])
    comp_places = {}
    for p in grid.places:
        c = place_cells.get(p["id"])
        if c is not None:
            comp_places.setdefault(comp[c], []).append(p["id"])
    components_report = []
    for gid, cells in enumerate(groups):
        area = len(cells) * cell_area
        components_report.append({
            "id": gid, "cells": len(cells), "area_m2": round(area, 1),
            "has_start": gid in comp_of_start, "places": comp_places.get(gid, []),
            "stand_min": round(min(grid.stand[c] for c in cells), 2),
            "stand_max": round(max(grid.stand[c] for c in cells), 2)})
        if gid in comp_of_start or comp_places.get(gid):
            continue
        if area >= float(opts["min_pocket_area"]):
            cell_set = set(cells)
            if not any(all(nb in cell_set for nb in grid.neighbors(c)) for c in cells):
                continue  # a thin strip or ring (a wall top), not a region
            cx = sum(grid.cell_center(c)[0] for c in cells) / len(cells)
            cz = sum(grid.cell_center(c)[1] for c in cells) / len(cells)
            stand_min = min(grid.stand[c] for c in cells)
            srcs = [reach_field[c] for c in cells if not math.isnan(reach_field[c])]
            elevated = srcs and stand_min - min(srcs) > body_h
            findings.append(_finding(
                "elevated_region" if elevated else "pocket",
                "info" if elevated else "warning",
                _round3([cx, stand_min, cz]),
                (f"elevated walkable surface of {area:.1f} m^2, reachable only by "
                 "falling onto it" if elevated else
                 f"walkable region of {area:.1f} m^2 with no start and no place"),
                area_m2=round(area, 1), component=gid))

    # ---- (1) coverage and overhang ---------------------------------
    for vi, (v, cells) in enumerate(zip(grid.visuals, vis.mesh_cells)):
        if cells is None:
            continue
        a = v["aabb"]
        tri = vis.mesh_tri_cells[vi]
        uncovered = []
        for c in cells:
            lo_b, hi_b = tri[c] if tri is not None else (a[1], a[4])
            # Covered when collision in this cell or a neighbour cell
            # supports the visual: it either meets the visual's own
            # span (within a 3 cm touch) or reaches up to within
            # 0.75 m under it -- a pitched roof's surface sits above
            # the flat collision top that carries it, eaves and wall
            # skins project up to a cell beyond their collision, and
            # parts sitting ON a top touch it with zero overlap.
            covered = False
            ci, cj = c % grid.width, c // grid.width
            for dj in (-1, 0, 1):
                for di in (-1, 0, 1):
                    ni, nj = ci + di, cj + dj
                    if not (0 <= ni < grid.width and 0 <= nj < grid.height):
                        continue
                    for lo, hi, _ in grid.intervals.get(nj * grid.width + ni) or []:
                        # A tall solid carries visuals further above
                        # its top (a roof's ridge trim rides ~2 m over
                        # the flat collision top); a low one (a pad, a
                        # bench) only what nearly touches it.
                        gap = 2.25 if hi - lo >= 1.5 else 0.75
                        if _overlaps(lo, hi, lo_b - gap, hi_b) > -0.03:
                            covered = True
                            break
                        # An elevated mesh (a roof) is also carried
                        # when nearby collision reaches its base level:
                        # the structure under it reaches the level the
                        # mesh sits on, even where its pitched surface
                        # rises far above the collision top.
                        if a[1] >= 1.0 and hi >= a[1] - 0.03 and lo <= hi_b:
                            covered = True
                            break
                    if covered:
                        break
                if covered:
                    break
            if not covered:
                uncovered.append(c)
        coverage = 1.0 - len(uncovered) / max(1, len(cells))
        if coverage < float(opts["coverage_min"]):
            pos = _largest_cluster_pos(grid, uncovered)
            big = len(uncovered) * cell_area >= 4.0
            findings.append(_finding(
                "visual_uncovered", "error" if coverage < 0.5 and big else "warning", pos,
                f"mesh '{v['path']}' has collision under only {coverage:.0%} of its footprint",
                objects=[v["path"]], coverage=round(coverage, 3),
                uncovered_area_m2=round(len(uncovered) * cell_area, 1)))
    solid_cells = {}
    for c, ivs in grid.intervals.items():
        for _, _, sid in ivs:
            solid_cells.setdefault(sid, set()).add(c)
    for sid, cells in solid_cells.items():
        s = grid.aabb_solids[sid]
        unexplained = []
        for c in cells:
            if not any(_overlaps(lo, hi, s["aabb"][1], s["aabb"][4]) > 0.05
                       for lo, hi, _ in vis.all_cells.get(c) or []):
                unexplained.append(c)
        ratio = len(unexplained) / max(1, len(cells))
        if ratio > float(opts["overhang_max"]) and len(unexplained) * cell_area >= 0.5:
            findings.append(_finding(
                "collision_overhang", "warning", _largest_cluster_pos(grid, unexplained),
                f"collision '{s['path']}' has no visual over {ratio:.0%} of its footprint "
                "(invisible wall / over-wide collision)",
                objects=[s["path"]], overhang=round(ratio, 3),
                unexplained_area_m2=round(len(unexplained) * cell_area, 1)))

    # ---- (3) leaks and plugged gaps --------------------------------
    # The intended graph: what the VISUALS connect. Traversable cells
    # a visual does not block, plus phantom cells (collision the
    # visuals do not know about counts as open), under the same
    # symmetric step rule as the actual components -- so a wall's top
    # does not bridge its two sides.
    step_o = float(opts["step"])
    virt = {}
    for c in range(grid.width * grid.height):
        if grid.traversable[c] and c not in hole:
            virt[c] = grid.stand[c]
    for c in phantom:
        if not math.isnan(grid.floor_ref[c]):
            virt[c] = grid.floor_ref[c]

    def virt_edge(a, b):
        if a in phantom or b in phantom:
            return abs(virt[a] - virt[b]) <= step_o + 1e-9
        return grid.move_allowed(a, b) and grid.move_allowed(b, a)

    intended_comp = [-1] * (grid.width * grid.height)
    n_intended = 0
    for c in virt:
        if intended_comp[c] >= 0:
            continue
        queue = deque([c])
        intended_comp[c] = n_intended
        while queue:
            cur = queue.popleft()
            for nb in grid.neighbors(cur):
                if nb in virt and intended_comp[nb] < 0 and virt_edge(cur, nb):
                    intended_comp[nb] = n_intended
                    queue.append(nb)
        n_intended += 1
    actual_of_intended = {}
    for c in virt:
        if grid.traversable[c] and c not in hole:
            actual_of_intended.setdefault(intended_comp[c], set()).add(comp[c])
    for cluster in _cluster_cells(hole, grid.width):
        area = len(cluster) * cell_area
        adjacent = set()
        for c in cluster:
            for nb in grid.neighbors(c):
                if nb in virt and grid.traversable[nb]:
                    adjacent.add(intended_comp[nb])
        cx = sum(grid.cell_center(c)[0] for c in cluster) / len(cluster)
        cz = sum(grid.cell_center(c)[1] for c in cluster) / len(cluster)
        cy = sum(grid.stand[c] for c in cluster) / len(cluster)
        paths = sorted({p for c in cluster for lo, hi, p in vis.solid_cells.get(c) or []
                        if _overlaps(lo, hi, grid.stand[c], grid.stand[c] + body_h) > 0.3})
        if len(adjacent) >= 2:
            findings.append(_finding(
                "wall_leak", "error", _round3([cx, cy, cz]),
                f"a {area:.1f} m^2 hole in the collision lets a walker cross a visual "
                "barrier between two regions the visuals keep apart (すり抜け)",
                objects=paths, area_m2=round(area, 1)))
        elif area >= 0.5:
            findings.append(_finding(
                "visual_without_collision", "warning", _round3([cx, cy, cz]),
                f"visual geometry over {area:.1f} m^2 can be walked through "
                "(no collision in the body band)",
                objects=paths, area_m2=round(area, 1)))
    for cluster in _cluster_cells(phantom, grid.width):
        area = len(cluster) * cell_area
        if area < 0.5:
            continue
        adjacent_actual = set()
        for c in cluster:
            for nb in grid.neighbors(c):
                if grid.traversable[nb]:
                    adjacent_actual.add(comp[nb])
        shared = set()
        for ac in adjacent_actual:
            for ic, acs in actual_of_intended.items():
                if ac in acs:
                    shared.add(ic)
        cx = sum(grid.cell_center(c)[0] for c in cluster) / len(cluster)
        cz = sum(grid.cell_center(c)[1] for c in cluster) / len(cluster)
        paths = sorted({grid.aabb_solids[s]["path"] for c in cluster
                        for _, _, s in grid.intervals.get(c) or []})
        if len(adjacent_actual) >= 2 and len(shared) == 1:
            findings.append(_finding(
                "plugged_gap", "warning", _round3([cx, 0.0, cz]),
                f"invisible collision over {area:.1f} m^2 plugs a passage the visuals "
                "leave open (塞ぎ)", objects=paths, area_m2=round(area, 1)))
        else:
            findings.append(_finding(
                "phantom_collision", "info", _round3([cx, 0.0, cz]),
                f"collision over {area:.1f} m^2 has no visual (invisible obstacle)",
                objects=paths, area_m2=round(area, 1)))

    # ---- (4) overlap ------------------------------------------------
    solids = grid.aabb_solids
    for i in range(len(solids)):
        for j in range(i + 1, len(solids)):
            a, b = solids[i], solids[j]
            if a["body_key"] == b["body_key"]:
                continue
            ix = _overlaps(a["aabb"][0], a["aabb"][3], b["aabb"][0], b["aabb"][3])
            iy = _overlaps(a["aabb"][1], a["aabb"][4], b["aabb"][1], b["aabb"][4])
            iz = _overlaps(a["aabb"][2], a["aabb"][5], b["aabb"][2], b["aabb"][5])
            if ix <= 0 or iy <= 0 or iz <= 0:
                continue
            vol = ix * iy * iz
            va = (a["aabb"][3] - a["aabb"][0]) * (a["aabb"][4] - a["aabb"][1]) * (a["aabb"][5] - a["aabb"][2])
            vb = (b["aabb"][3] - b["aabb"][0]) * (b["aabb"][4] - b["aabb"][1]) * (b["aabb"][5] - b["aabb"][2])
            if vol >= 0.02 and vol >= 0.15 * min(va, vb):
                findings.append(_finding(
                    "collision_overlap", "warning",
                    _round3([(max(a["aabb"][0], b["aabb"][0]) + min(a["aabb"][3], b["aabb"][3])) / 2,
                             (max(a["aabb"][1], b["aabb"][1]) + min(a["aabb"][4], b["aabb"][4])) / 2,
                             (max(a["aabb"][2], b["aabb"][2]) + min(a["aabb"][5], b["aabb"][5])) / 2]),
                    f"collision of '{a['path']}' and '{b['path']}' overlap by {vol:.2f} m^3 "
                    "(double-defined collision)",
                    objects=[a["path"], b["path"]], volume_m3=round(vol, 3)))

    # ---- raster for the heatmap ------------------------------------
    raster = bytearray(grid.width * grid.height)
    for c in range(grid.width * grid.height):
        if c in hole:
            raster[c] = 5
        elif c in phantom:
            raster[c] = 2
        elif grid.traversable[c]:
            raster[c] = 4 if c in reached_any else 3
        elif c in grid.blockers:
            raster[c] = 1
    for p in grid.places:
        c = place_cells.get(p["id"])
        if c is not None:
            raster[c] = 6

    summary = {"findings": len(findings),
               "errors": sum(1 for f in findings if f["severity"] == "error"),
               "warnings": sum(1 for f in findings if f["severity"] == "warning"),
               "info": sum(1 for f in findings if f["severity"] == "info"),
               "by_kind": {}}
    for f in findings:
        summary["by_kind"][f["kind"]] = summary["by_kind"].get(f["kind"], 0) + 1
    counts = extract.get("counts") or {}
    report = {
        "format": REPORT_FORMAT,
        "scene": extract.get("scene"),
        "ok": summary["errors"] == 0,
        "params": {k: opts[k] for k in DEFAULTS},
        "grid": {"cell": grid.cell, "width": grid.width, "height": grid.height,
                 "origin": [round(grid.minx, 2), round(grid.minz, 2)],
                 "traversable_cells": int(sum(grid.traversable)),
                 "components": len(groups)},
        "starts": start_report,
        "places": places_report,
        "components": sorted(components_report, key=lambda c: -c["area_m2"])[:20],
        "summary": summary,
        "findings": findings,
        "notes": [
            "static geometric check on a grid: AABB footprints stand in for rotated "
            "shapes, heightfields are sampled at cell centres, and multi-storey "
            "interiors collapse to their highest standable surface; the dynamic "
            "proof is the physics walker (B-1510)",
            "the body fit keeps passages of roughly char_width + cell open and "
            "closes narrower ones, so gaps between char_width and that width are "
            "reported closed: the grid cannot certify a gap it cannot resolve",
            f"extraction: {counts.get('shapes', 0)} static shapes, "
            f"{counts.get('shapes_disabled', 0)} disabled, {counts.get('areas', 0)} areas "
            f"(not blockers), {counts.get('other_bodies', 0)} non-static shapes skipped",
        ] + ([f"unsupported shape at {u['path']}: {u['class']}"
              for u in counts.get("unsupported") or []]),
    }
    return report, {"width": grid.width, "height": grid.height, "classes": raster,
                    "cell": grid.cell, "origin": [grid.minx, grid.minz]}


def _line_block(grid, ca, cb):
    """First blockage on the straight line from cell ca to cell cb:
    a non-traversable cell, or a cross-surface rise over the step
    limit. None when the line walks clean."""
    ax, az = grid.cell_center(ca)
    bx, bz = grid.cell_center(cb)
    dist = math.hypot(bx - ax, bz - az)
    n = max(2, int(dist / (grid.cell * 0.5)))
    step = float(grid.opts["step"])
    prev = None  # (stand, source)
    for k in range(n + 1):
        x = ax + (bx - ax) * k / n
        z = az + (bz - az) * k / n
        c = grid.cell_at(x, z)
        if c is None:
            continue
        if not grid.traversable[c]:
            y = grid.stand[c] if not math.isnan(grid.stand[c]) else grid.floor_ref[c]
            rise = None
            why = "the line crosses ground the walker cannot occupy"
            if prev is not None:
                ci, cj = c % grid.width, c // grid.width
                tops = []
                for dj in (-1, 0, 1):
                    for di in (-1, 0, 1):
                        ni, nj = ci + di, cj + dj
                        if 0 <= ni < grid.width and 0 <= nj < grid.height:
                            tops += [hi for _, hi, _ in
                                     grid.intervals.get(nj * grid.width + ni) or []
                                     if hi > prev[0]]
                if tops:
                    rise = round(max(tops) - prev[0], 2)
                    if rise > step:
                        why = (f"a ledge of {rise} m exceeds the {step} m step "
                               "at the line")
            return {"at": [round(x, 2), round(y, 2) if not math.isnan(y) else None,
                           round(z, 2)],
                    "why": why, "rise": rise}
        s, src = grid.stand[c], grid.stand_source[c]
        if prev is not None and src != prev[1] and s - prev[0] > step:
            return {"at": [round(x, 2), round(s, 2), round(z, 2)],
                    "why": f"a ledge of {round(s - prev[0], 2)} m exceeds the "
                           f"{step} m step at the line",
                    "rise": round(s - prev[0], 2)}
        prev = (s, src)
    return None


def _reach_field(grid, reached):
    """For every cell, the stand height of the nearest reached cell
    (multi-source BFS over all cells, ignoring traversability): the
    local ground level an unreached surface is compared against for
    the elevated test."""
    nan = float("nan")
    field = [nan] * (grid.width * grid.height)
    queue = deque()
    for c in reached:
        field[c] = grid.stand[c]
        queue.append(c)
    while queue:
        cur = queue.popleft()
        for nb in grid.neighbors(cur):
            if math.isnan(field[nb]):
                field[nb] = field[cur]
                queue.append(nb)
    return field


def _bottleneck(grid, fill, target_comp, target_cell):
    """Why target's component is cut off from fill: the cheapest
    step-blocked crossing near their boundary (cells up to 2 apart --
    body-fit inflation can hold the fill one cell back), else a note
    that the regions share no boundary at all."""
    best = None
    w = grid.width
    for c in fill:
        ci, cj = c % w, c // w
        for dj in range(-2, 3):
            for di in range(-2, 3):
                if di == 0 and dj == 0:
                    continue
                ni, nj = ci + di, cj + dj
                if not (0 <= ni < w and 0 <= nj < grid.height):
                    continue
                nb = nj * w + ni
                if nb in fill or not grid.traversable[nb]:
                    continue
                rise = grid.stand[nb] - grid.stand[c]
                if best is None or rise < best[0]:
                    x1, z1 = grid.cell_center(c)
                    x2, z2 = grid.cell_center(nb)
                    best = (rise, [(x1 + x2) / 2, grid.stand[nb], (z1 + z2) / 2],
                            grid.stand[c], grid.stand[nb])
    if best is not None:
        return {"bottleneck": _round3(best[1]), "required_rise_m": round(best[0], 2),
                "from_stand": round(best[2], 2), "to_stand": round(best[3], 2),
                "step_m": float(grid.opts["step"])}
    return {"bottleneck": None,
            "note": "no shared boundary of traversable cells; the regions are "
                    "separated by blocked cells"}


def _largest_cluster_pos(grid, cells):
    if not cells:
        return None
    clusters = _cluster_cells(set(cells), grid.width)
    biggest = max(clusters, key=len)
    cx = sum(grid.cell_center(c)[0] for c in biggest) / len(biggest)
    cz = sum(grid.cell_center(c)[1] for c in biggest) / len(biggest)
    return _round3([cx, 0.0, cz])


# ------------------------------------------------------------------ heatmap

_COLORS = {
    0: (30, 30, 30),        # no surface
    1: (95, 105, 135),      # blocked, a visual explains it
    2: (215, 60, 215),      # phantom collision
    3: (240, 165, 60),      # walkable, not reached (pocket side)
    4: (85, 175, 85),       # walkable, reached
    5: (225, 55, 55),       # hole: visual without collision
    6: (255, 255, 255),     # place
}


def write_heatmap(raster, path):
    """Top-down PNG of the check raster: x to the right, -z up (row 0
    is the smallest z). Small grids are scaled up by an integer factor
    so the map stays readable."""
    w, h = raster["width"], raster["height"]
    scale = 1
    while w * (scale + 1) <= 1024 and h * (scale + 1) <= 1024 and scale < 8:
        scale += 1
    rows = []
    for j in range(h):
        row = bytearray()
        for i in range(w):
            r, g, b = _COLORS[raster["classes"][j * w + i]]
            row += bytes((r, g, b)) * scale
        for _ in range(scale):
            rows.append(b"\x00" + bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    return {"path": path, "width": w * scale, "height": h * scale, "scale": scale,
            "legend": {"green": "walkable, reached from a start",
                       "orange": "walkable, not reached",
                       "red": "hole: visual without collision",
                       "magenta": "phantom collision (no visual)",
                       "slate": "blocked, explained by a visual",
                       "dark": "no standable surface",
                       "white": "place"}}


# ---------------------------------------------------------------------- CLI


def main(argv):
    extract_path = out_path = heatmap_path = None
    starts = []
    opts = {}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--extract" and i + 1 < len(argv):
            extract_path, i = argv[i + 1], i + 2
        elif a == "--out" and i + 1 < len(argv):
            out_path, i = argv[i + 1], i + 2
        elif a == "--heatmap" and i + 1 < len(argv):
            heatmap_path, i = argv[i + 1], i + 2
        elif a == "--start" and i + 1 < len(argv):
            x, z = argv[i + 1].split(",")
            starts.append((float(x), float(z), f"start[{len(starts)}]"))
            i += 2
        elif a.startswith("--") and i + 1 < len(argv) and a[2:].replace("-", "_") in DEFAULTS:
            opts[a[2:].replace("-", "_")] = float(argv[i + 1])
            i += 2
        else:
            i += 1
    if not extract_path:
        print(json.dumps({"ok": False, "error": "usage: collision_check.py --extract extract.json "
                                                "[--out report.json] [--heatmap map.png] [--start x,z ...]"}))
        return 1
    with open(extract_path, encoding="utf-8") as f:
        extract = json.load(f)
    report, raster = analyze(extract, opts, starts or None)
    if heatmap_path:
        report["heatmap"] = write_heatmap(raster, heatmap_path)
    if out_path:
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=2)
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
