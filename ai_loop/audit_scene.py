#!/usr/bin/env python3
"""Re-read audit of a saved scene (BACKLOG B-1511): the comparison half.

The engine side (ai_loop/audit_tool.gd) re-opens a saved .tscn/.scn and
counts what is actually in the artifact -- mesh triangles, textures with
dimensions, placed instances, collision shapes and bodies, lights,
navigation meshes -- without asking the generator anything. This module
compares those recounts with the generation spec set (B-1509:
asset_registry.json + scene_spec.json) and returns the differences as
structured output. Where the registry claims something the artifact
cannot express, the check is reported "not_countable" with the reason,
never silently dropped.

Recount rules (the artifact side of each comparison; the per-node
breakdown is in the report's "measured", so any number can be recomputed
under another convention):

  family with measure "triangles"
    - "terrain_mesh" (nature): the triangles of the mesh node named
      "Terrain" in the part (the nature generator's terrain node).
    - "meshes" (city): every mesh node's triangles in the part plus the
      MultiMesh triangles, minus the triangles of MultiMeshes whose
      instances are themselves a counted scatter family (street trees,
      trees, rocks, grass -- the generator's total excludes them);
      window-frame MultiMeshes stay in, because the generator's total
      includes the frames as building parts.
    - any other: all placed triangles of the part.
  family counted in instances (no measure)
    - a MultiMesh family: MultiMesh nodes in the part whose normalized
      name matches the family (exact, else a unique stem-prefix group:
      "trees" -> TreesBroadleaf + TreesConifer, "windows" ->
      WindowFrames_*). Instances are counted AS STORED in the artifact
      (for a text .tscn, the MultiMesh buffer in the file; the engine's
      loaded count is reported alongside and a disagreement between the
      two is its own load_fidelity check).
    - a collision family: CollisionShape3D nodes whose name, stripped of
      its trailing index, is the family's singular (Building0 ->
      buildings, Bench3 -> benches, Block_0_0 -> blocks).
    - "lights": Light3D nodes in the part.
    - a family with no matching structure anywhere counts as 0, so a
      deleted part surfaces as a difference; expected 0 is then a match
      (its absence is the recount). The exception is NOT_COUNTABLE
      below: registry entries that count things leaving no per-instance
      structure in the scene by construction (lamps -- the posts are
      merged into one mesh, windows_lit, empty_lots, the "parts"
      breakdown, buildings_detail): not_countable, with the reason in
      the check's note.
  scene_spec "observed"
    - navigation.polygons / navigation.vertices are recounted from the
      part's NavigationRegion3D resources. Place positions and counts
      duplicate the registry and are not rechecked here.

Binary scenes (.scn, or resources saved outside the .tscn) carry no
text to parse: stored MultiMesh counts then fall back to the engine's
loaded counts (the binary format stores instance_count; verified on
map_tool's external .res saves), and load_fidelity checks are skipped.

uzu.py audit-scene is the entry point; this module is also a small CLI
for tests: audit_scene.py --recount recount.json --scene-file x.tscn
[--spec-set DIR] prints the report.
Standard library only.
"""

import json
import os
import re
import sys

REPORT_FORMAT = "uzu-scene-audit/1"


# ------------------------------------------------------------------ helpers


def _norm(name):
    """Node/family name key: lowercase alphanumerics only."""
    return re.sub(r"[^a-z0-9]", "", str(name).lower())


def _stem(name):
    n = _norm(name)
    return n[:-1] if n.endswith("s") and len(n) > 1 else n


def _shape_key(name):
    """Collision shape name -> family key: 'Building0' -> 'building',
    'Block_0_0' -> 'block', 'Bench3' -> 'bench'."""
    return _norm(re.sub(r"[0-9_]+$", "", str(name)))


def _family_keys(family):
    n = _norm(family)
    keys = {n, _stem(family)}
    if n.endswith("es"):
        keys.add(n[:-2])
    return keys


# ------------------------------------------------------- text .tscn parsing


_TSCN_SUB = re.compile(r'\[sub_resource type="([^"]+)" id="([^"]+)"\]\n(.*?)(?=\n\[|\Z)', re.S)
_TSCN_NODE = re.compile(r'\[node name="([^"]+)" type="([^"]+)"(?: parent="([^"]*)")?[^\]]*\]\n(.*?)(?=\n\[|\Z)', re.S)


def _float_count(text):
    text = text.strip()
    return 0 if not text else text.count(",") + 1


def parse_tscn_multimeshes(path):
    """{node_path: {"stored": int, "instance_count_prop": int|None,
    "buffer_instances": int|None}} for a TEXT .tscn, else None.

    A MultiMesh's instances as stored: the instance_count property when
    the file carries one, else the buffer's float count divided by the
    per-instance stride (transform 8/12 floats by format, +4 for colors,
    +4 for custom data). Node paths are scene-root relative, as the
    engine recount reports them."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    if not text.lstrip().startswith("[gd_scene"):
        return None
    subs = {}
    for m in _TSCN_SUB.finditer(text):
        if m.group(1) != "MultiMesh":
            continue
        body = m.group(3)
        fmt = re.search(r"^transform_format = (\d+)", body, re.M)
        stride = 8 if (fmt and fmt.group(1) == "0") else 12
        if re.search(r"^use_colors = true", body, re.M):
            stride += 4
        if re.search(r"^use_custom_data = true", body, re.M):
            stride += 4
        prop = re.search(r"^instance_count = (\d+)", body, re.M)
        buf = re.search(r"^buffer = PackedFloat32Array\((.*?)\)\s*$", body, re.M | re.S)
        floats = _float_count(buf.group(1)) if buf else 0
        subs[m.group(2)] = {
            "instance_count_prop": int(prop.group(1)) if prop else None,
            "buffer_instances": (floats // stride) if floats else None,
            "stride": stride,
        }
    out = {}
    for m in _TSCN_NODE.finditer(text):
        name, ntype, parent, body = m.group(1), m.group(2), m.group(3), m.group(4)
        if ntype != "MultiMeshInstance3D" or parent is None:
            continue
        mm = re.search(r'^multimesh = SubResource\("([^"]+)"\)', body, re.M)
        if not mm or mm.group(1) not in subs:
            continue
        sub = subs[mm.group(1)]
        stored = sub["instance_count_prop"]
        if stored is None:
            stored = sub["buffer_instances"]
        node_path = name if parent == "." else parent + "/" + name
        out[node_path] = {"stored": stored, **sub}
    return out


# ----------------------------------------------------------------- spec set


def load_spec_set(spec_dir):
    """{"registry": [family...], "observed": {part|None: {name: value}},
    "scene": str|None} from a B-1509 spec set folder, or None."""
    if not spec_dir:
        return None
    reg_path = os.path.join(spec_dir, "asset_registry.json")
    spec_path = os.path.join(spec_dir, "scene_spec.json")
    if not os.path.isfile(reg_path):
        return None
    with open(reg_path, encoding="utf-8") as f:
        registry = json.load(f)
    observed = {}
    scene = registry.get("scene")
    if os.path.isfile(spec_path):
        with open(spec_path, encoding="utf-8") as f:
            spec = json.load(f)
        scene = spec.get("scene", scene)
        if spec.get("kind") == "composite":
            for part in spec.get("parts") or []:
                observed[part.get("part")] = {o["name"]: o.get("value") for o in part.get("observed") or []}
        else:
            observed[None] = {o["name"]: o.get("value") for o in spec.get("observed") or []}
    return {"registry": registry.get("families") or [], "observed": observed, "scene": scene}


# ------------------------------------------------------------------- checks


def _scope_parts(measured_parts, part):
    """[(part_name, counts)] a family is recounted over: its own part, or
    every part when the registry does not name one."""
    if part is not None:
        return [(part, measured_parts.get(part) or {})]
    return sorted(measured_parts.items())


def _mmi_actuals(scope):
    """All MultiMesh recounts in scope: [{path, part, name, stored,
    loaded, per_instance_triangles}]."""
    out = []
    for part_name, counts in scope:
        for node in counts.get("multimesh_nodes") or []:
            out.append({**node, "part": part_name})
    return out


def _match_mmis(family, mmis):
    """The MultiMeshes an instance family is recounted from: exact
    normalized name match, else the stem-prefix group."""
    fam = _norm(family)
    exact = [m for m in mmis if _norm(m["name"]) == fam]
    if exact:
        return exact
    stem = _stem(family)
    return [m for m in mmis if _norm(m["name"]).startswith(stem)]


def _check(cid, kind, status, expected, actual, note, **extra):
    c = {"id": cid, "kind": kind, "status": status,
         "expected": expected, "actual": actual, "note": note}
    c.update(extra)
    return c


def _registry_checks(measured_parts, families):
    checks = []
    # First pass: instance families, so the triangle total knows which
    # MultiMeshes are scatter (counted as instances, not as city mesh).
    scatter_paths = set()
    instance_actual = {}
    for fam in families:
        if fam.get("measure") == "triangles" or not isinstance(fam.get("count"), (int, float)):
            continue
        scope = _scope_parts(measured_parts, fam.get("part"))
        mmis = _match_mmis(fam["family"], _mmi_actuals(scope))
        if mmis:
            instance_actual[id(fam)] = sum(m["stored"] for m in mmis)
            if fam["family"] != "windows":  # window frames stay in the city mesh total
                scatter_paths.update((m["part"], m["path"]) for m in mmis)
    for fam in families:
        family, part, count = fam["family"], fam.get("part"), fam.get("count")
        cid = f"{part or '*'}/{family}"
        scope = _scope_parts(measured_parts, part)
        if fam.get("count_basis") not in (None, "observed"):
            checks.append(_check(cid, "registry", "not_countable", count, None,
                                 f"count_basis is {fam.get('count_basis')!r}: a spec-side number, not a generator measurement"))
            continue
        if isinstance(count, dict):
            checks.append(_check(cid, "registry", "not_countable", count, None,
                                 "a per-part breakdown whose parts are merged into shared meshes in the artifact; "
                                 "its window_frames entry is recounted by the 'windows' family"))
            continue
        if fam.get("measure") == "triangles":
            if family == "terrain_mesh":
                actual = sum(n["triangles"] for _, c in scope for n in c.get("mesh_nodes") or [] if n["name"] == "Terrain")
                note = "the 'Terrain' mesh node of the part"
            elif family == "meshes":
                mesh_tris = sum(c.get("mesh_triangles", 0) for _, c in scope)
                mmi_tris = sum(m["per_instance_triangles"] * m["stored"] for m in _mmi_actuals(scope)
                               if (m["part"], m["path"]) not in scatter_paths)
                actual = mesh_tris + mmi_tris
                note = "mesh nodes plus MultiMesh triangles, scatter MultiMeshes excluded (they are counted as instances)"
            else:
                actual = sum(c.get("mesh_triangles", 0) + sum(
                    m["per_instance_triangles"] * m["stored"] for m in _mmi_actuals([(p, c)]))
                    for p, c in scope)
                note = "all placed triangles of the part"
            checks.append(_check(cid, "registry", "match" if actual == count else "difference",
                                 count, actual, note))
            continue
        # Instance-counted families.
        if id(fam) in instance_actual:
            actual = instance_actual[id(fam)]
            checks.append(_check(cid, "registry", "match" if actual == count else "difference",
                                 count, actual, "MultiMesh instances as stored in the artifact"))
            continue
        shape_names = [name for _, c in scope for name in c.get("collision_shape_names") or []]
        by_shape = sum(1 for name in shape_names if _shape_key(name) in _family_keys(family))
        if by_shape:
            checks.append(_check(cid, "registry", "match" if by_shape == count else "difference",
                                 count, by_shape, "CollisionShape3D nodes named for the family"))
            continue
        if family == "lights":
            actual = sum(c.get("lights", 0) for _, c in scope)
            checks.append(_check(cid, "registry", "match" if actual == count else "difference",
                                 count, actual, "Light3D nodes in the part"))
            continue
        reasons = {
            "lamps": "the lamp posts are merged into one mesh; no per-post structure survives in the artifact",
            "windows_lit": "lit windows are merged into one mesh; the artifact holds no per-window structure",
            "empty_lots": "an empty lot is the absence of a building on a block; not a structure in the artifact",
            "buildings_detail": "the registry entry counts the keys of the generator's detail dictionary, not a scene quantity",
        }
        if family in reasons:
            checks.append(_check(cid, "registry", "not_countable", count, None, reasons[family]))
            continue
        checks.append(_check(cid, "registry", "match" if count == 0 else "difference", count, 0,
                             "no structure of this family exists in the artifact; the absence is the recount"))
    return checks


def _observed_checks(measured_parts, observed_by_part):
    checks = []
    for part, observed in sorted(observed_by_part.items(), key=lambda kv: str(kv[0])):
        scope = _scope_parts(measured_parts, part)
        for key in ("navigation.polygons", "navigation.vertices"):
            if key not in observed:
                continue
            field = key.split(".")[1]
            actual = sum(n.get(field, 0) for _, c in scope for n in c.get("navigation") or [])
            checks.append(_check(f"{part or '*'}/{key}", "observed",
                                 "match" if actual == observed[key] else "difference",
                                 observed[key], actual,
                                 "NavigationRegion3D resources in the part"))
    return checks


def _fidelity_checks(measured_parts, stored_by_path):
    checks = []
    if stored_by_path is None:
        return checks
    for part_name, counts in sorted(measured_parts.items()):
        for node in counts.get("multimesh_nodes") or []:
            stored = stored_by_path.get(node["path"])
            if stored is None or stored["stored"] is None:
                continue
            if stored["stored"] != node["instances"]:
                note = ("the file stores %d instances but the engine loads %d"
                        % (stored["stored"], node["instances"]))
                if stored["instance_count_prop"] is None:
                    note += ("; the .tscn carries the MultiMesh buffer but no instance_count property, "
                             "and the loader drops a buffer set while the count is still 0")
                checks.append(_check(f"{part_name}/load_fidelity/{node['name']}", "load_fidelity",
                                     "difference", stored["stored"], node["instances"], note,
                                     node=node["path"]))
    return checks


# -------------------------------------------------------------------- build


def build_report(scene, recount, stored_by_path, spec_set):
    """The audit report (format uzu-scene-audit/1) from the engine
    recount (audit_tool.gd), the text-scene stored counts (or None) and
    the loaded spec set (or None)."""
    parts = recount.get("parts") or {}
    # Merge the stored MultiMesh counts into the measured parts: the
    # engine's per-node "instances" is the loaded count; "stored" is what
    # the artifact itself carries.
    for part_name, counts in parts.items():
        for node in counts.get("multimesh_nodes") or []:
            stored = (stored_by_path or {}).get(node["path"])
            node["stored_instances"] = stored["stored"] if stored else None
            node["stored"] = node["stored_instances"] if node["stored_instances"] is not None else node["instances"]
    checks = []
    if spec_set:
        checks += _registry_checks(parts, spec_set["registry"])
        checks += _observed_checks(parts, spec_set["observed"])
    checks += _fidelity_checks(parts, stored_by_path)
    summary = {"match": 0, "difference": 0, "not_countable": 0}
    for c in checks:
        summary[c["status"]] = summary.get(c["status"], 0) + 1
    totals = recount.get("totals") or {}
    collision = {
        "shapes": totals.get("collision_shapes", 0),
        "bodies": totals.get("collision_bodies", 0),
        "parts": {p: {"shapes": c.get("collision_shapes", 0), "bodies": c.get("collision_bodies", 0)}
                  for p, c in sorted(parts.items())},
        "parts_with_collision": sorted(p for p, c in parts.items() if c.get("collision_shapes", 0) > 0),
    }
    return {
        "format": REPORT_FORMAT,
        "scene": scene,
        "spec_set_scene": spec_set["scene"] if spec_set else None,
        "ok": summary["difference"] == 0,
        "summary": summary,
        "checks": checks,
        "measured": {
            "totals": totals,
            "parts": parts,
            "textures": recount.get("textures") or [],
            "texture_count": len(recount.get("textures") or []),
            "collision": collision,
        },
        "notes": [
            "counts come from re-opening the saved scene (ai_loop/audit_tool.gd) and, for MultiMesh "
            "instances in a text .tscn, from the buffers stored in the file; generator output is "
            "never used as evidence",
            "the collision block is the recount B-1509's validation_report lacked for its "
            "collision_coverage check (left Unverified there)",
        ],
    }


def main(argv):
    recount_path = scene_file = spec_dir = None
    i = 0
    while i < len(argv):
        if argv[i] == "--recount" and i + 1 < len(argv):
            recount_path, i = argv[i + 1], i + 2
        elif argv[i] == "--scene-file" and i + 1 < len(argv):
            scene_file, i = argv[i + 1], i + 2
        elif argv[i] == "--spec-set" and i + 1 < len(argv):
            spec_dir, i = argv[i + 1], i + 2
        else:
            i += 1
    if not recount_path or not scene_file:
        print(json.dumps({"ok": False, "error": "usage: audit_scene.py --recount recount.json --scene-file x.tscn [--spec-set DIR]"}))
        return 1
    with open(recount_path, encoding="utf-8") as f:
        recount = json.load(f)
    stored = parse_tscn_multimeshes(scene_file)
    report = build_report(recount.get("scene"), recount, stored, load_spec_set(spec_dir))
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
