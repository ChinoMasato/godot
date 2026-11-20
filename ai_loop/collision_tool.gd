extends SceneTree
## Collision extraction, engine side (BACKLOG B-1224): loads a SAVED
## scene (.tscn / .scn) through the resource loader, instantiates it
## WITHOUT adding it to the tree (no _ready runs, nothing is played),
## and extracts the raw geometry the mechanical collision check runs
## on: collision solids (world AABBs for primitive shapes, heightfield
## data for HeightMapShape3D, world-space faces for
## ConcavePolygonShape3D, planes for WorldBoundaryShape3D), visual
## footprints (MeshInstance3D world-space triangles -- a merged mesh's
## AABB spans far more than its geometry -- plus its AABB as fallback,
## and MultiMesh per-instance AABBs) and the uzu_places metadata
## generated maps carry.
##
## Global transforms are composed by hand down the Node3D ancestor
## chain (the instance is not in the tree, so global_transform is not
## maintained). Non-Node3D nodes pass the chain through, as the engine
## does. Nothing here interprets the geometry: the checks live in
## ai_loop/collision_check.py; this script only measures.
##
##   godot --headless --path <project> --script collision_tool.gd -- --scene res://x.tscn
##   -> prints one line: COLLISION_RESULT {json}
##
## Only StaticBody3D shapes are solids. Area3D shapes are counted as
## areas (triggers, not blockers) and other body types are counted and
## skipped: the check is about the static world a walker moves through.

const MAX_TRIS_FLOATS := 300000 # cap on concave face data kept (100k triangles)
const MAX_VIS_FLOATS := 600000 # cap per mesh on visual triangle data kept

var _solids: Array = []
var _visuals: Array = []
var _multimeshes: Array = []
var _places: Array = []
var _counts := {"shapes": 0, "shapes_disabled": 0, "areas": 0, "other_bodies": 0, "unsupported": [], "visual_tris_unavailable": []}

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var scene_path := ""
	var i := 0
	while i < args.size():
		if args[i] == "--scene" and i + 1 < args.size():
			scene_path = args[i + 1]
			i += 2
		else:
			i += 1
	if scene_path == "":
		print("COLLISION_RESULT ", JSON.stringify({"ok": false, "error": "needs --scene res://..."}))
		quit(1)
		return
	var packed: PackedScene = load(scene_path) as PackedScene
	if packed == null:
		print("COLLISION_RESULT ", JSON.stringify({"ok": false, "error": "could not load %s as a PackedScene" % scene_path}))
		quit(1)
		return
	var root := packed.instantiate()
	if root == null:
		print("COLLISION_RESULT ", JSON.stringify({"ok": false, "error": "could not instantiate %s" % scene_path}))
		quit(1)
		return
	_walk(root, ".", Transform3D.IDENTITY, null)
	print("COLLISION_RESULT ", JSON.stringify({
		"ok": true, "scene": scene_path, "root": root.name,
		"solids": _solids, "visuals": _visuals, "multimeshes": _multimeshes,
		"places": _places, "counts": _counts,
	}))
	root.free()
	quit()

func _walk(node: Node, path: String, parent_global: Transform3D, body: CollisionObject3D) -> void:
	var global := parent_global
	if node is Node3D:
		global = parent_global * (node as Node3D).transform
	var own_body := body
	if node is CollisionObject3D:
		own_body = node as CollisionObject3D
	if node is CollisionShape3D:
		_count_shape(node as CollisionShape3D, path, global, own_body)
	if node is MeshInstance3D:
		var mi := node as MeshInstance3D
		if mi.mesh != null:
			var aabb := global * mi.get_aabb()
			var entry := {"path": path, "aabb": _aabb_array(aabb)}
			entry["tris"] = _mesh_tris(mi, global, path)
			_visuals.append(entry)
	if node is MultiMeshInstance3D:
		_count_multimesh(node as MultiMeshInstance3D, path, global)
	if node.has_meta("uzu_places"):
		_count_places(node, path)
	for child in node.get_children():
		var child_path: String = String(child.name) if path == "." else path + "/" + String(child.name)
		_walk(child, child_path, global, own_body)

func _mesh_tris(mi: MeshInstance3D, global: Transform3D, path: String) -> Variant:
	# World-space triangle soup of the mesh, so the check can rasterize
	# the mesh's REAL footprint: a merged city mesh's AABB spans the
	# whole city, its triangles sit only where the geometry is. Null
	# when the surface arrays are unreadable or too large (the check
	# then falls back to the AABB and says so).
	var flat: Array = []
	for s in mi.mesh.get_surface_count():
		var arrays := mi.mesh.surface_get_arrays(s)
		if arrays.size() != Mesh.ARRAY_MAX:
			return null
		var verts: Variant = arrays[Mesh.ARRAY_VERTEX]
		if not (verts is PackedVector3Array) or (verts as PackedVector3Array).is_empty():
			return null
		var pv := verts as PackedVector3Array
		var idx: Variant = arrays[Mesh.ARRAY_INDEX]
		if idx is PackedInt32Array:
			var pi := idx as PackedInt32Array
			for k in range(0, pi.size() - 2, 3):
				for c in 3:
					var w := global * pv[pi[k + c]]
					flat.append(w.x)
					flat.append(w.y)
					flat.append(w.z)
		elif idx == null:
			for k in range(0, pv.size() - 2, 3):
				for c in 3:
					var w := global * pv[k + c]
					flat.append(w.x)
					flat.append(w.y)
					flat.append(w.z)
		else:
			return null
		if flat.size() > MAX_VIS_FLOATS:
			(_counts["visual_tris_unavailable"] as Array).append(path)
			return null
	return flat

func _aabb_array(a: AABB) -> Array:
	return [a.position.x, a.position.y, a.position.z,
		a.position.x + a.size.x, a.position.y + a.size.y, a.position.z + a.size.z]

func _count_shape(cs: CollisionShape3D, path: String, global: Transform3D, body: CollisionObject3D) -> void:
	if cs.disabled:
		_counts["shapes_disabled"] = int(_counts["shapes_disabled"]) + 1
		return
	if cs.shape == null or body == null:
		return
	if body is Area3D:
		_counts["areas"] = int(_counts["areas"]) + 1
		return
	if not (body is StaticBody3D):
		_counts["other_bodies"] = int(_counts["other_bodies"]) + 1
		return
	_counts["shapes"] = int(_counts["shapes"]) + 1
	var entry := {"path": path, "body": String(body.name), "body_class": body.get_class()}
	var shape := cs.shape
	if shape is BoxShape3D:
		var s := (shape as BoxShape3D).size
		entry["kind"] = "box"
		entry["aabb"] = _aabb_array(global * AABB(-s * 0.5, s))
	elif shape is SphereShape3D:
		var r := (shape as SphereShape3D).radius
		entry["kind"] = "sphere"
		entry["aabb"] = _aabb_array(global * AABB(Vector3(-r, -r, -r), Vector3(2 * r, 2 * r, 2 * r)))
	elif shape is CapsuleShape3D:
		var cap := shape as CapsuleShape3D
		entry["kind"] = "capsule"
		entry["aabb"] = _aabb_array(global * AABB(
			Vector3(-cap.radius, -cap.height * 0.5, -cap.radius),
			Vector3(2 * cap.radius, cap.height, 2 * cap.radius)))
	elif shape is CylinderShape3D:
		var cyl := shape as CylinderShape3D
		entry["kind"] = "cylinder"
		entry["aabb"] = _aabb_array(global * AABB(
			Vector3(-cyl.radius, -cyl.height * 0.5, -cyl.radius),
			Vector3(2 * cyl.radius, cyl.height, 2 * cyl.radius)))
	elif shape is ConvexPolygonShape3D:
		var pts := (shape as ConvexPolygonShape3D).points
		if pts.is_empty():
			return
		var aabb := AABB(global * pts[0], Vector3.ZERO)
		for p in pts:
			aabb = aabb.expand(global * p)
		entry["kind"] = "convex"
		entry["aabb"] = _aabb_array(aabb)
	elif shape is HeightMapShape3D:
		var hm := shape as HeightMapShape3D
		entry["kind"] = "heightmap"
		entry["origin"] = [global.origin.x, global.origin.y, global.origin.z]
		entry["basis"] = [
			global.basis.x.x, global.basis.x.y, global.basis.x.z,
			global.basis.y.x, global.basis.y.y, global.basis.y.z,
			global.basis.z.x, global.basis.z.y, global.basis.z.z]
		entry["map_width"] = hm.map_width
		entry["map_depth"] = hm.map_depth
		var data: Array = []
		data.assign(hm.map_data)
		entry["data"] = data
	elif shape is ConcavePolygonShape3D:
		var faces := (shape as ConcavePolygonShape3D).get_faces()
		var n := mini(faces.size(), MAX_TRIS_FLOATS - MAX_TRIS_FLOATS % 3)
		var flat: Array = []
		for k in range(0, n, 3):
			for c in 3:
				var w := global * faces[k + c]
				flat.append(w.x)
				flat.append(w.y)
				flat.append(w.z)
		entry["kind"] = "tris"
		entry["faces"] = flat
		entry["truncated"] = faces.size() > n
	elif shape is WorldBoundaryShape3D:
		var plane := (shape as WorldBoundaryShape3D).plane
		var wplane := global * plane
		entry["kind"] = "plane"
		entry["normal"] = [wplane.normal.x, wplane.normal.y, wplane.normal.z]
		entry["d"] = wplane.d
	else:
		(_counts["unsupported"] as Array).append({"path": path, "class": shape.get_class()})
		return
	_solids.append(entry)

func _count_multimesh(mmi: MultiMeshInstance3D, path: String, global: Transform3D) -> void:
	if mmi.multimesh == null or mmi.multimesh.mesh == null:
		return
	var mm := mmi.multimesh
	var local_aabb := mm.mesh.get_aabb()
	var instances: Array = []
	for k in mm.instance_count:
		var aabb := (global * mm.get_instance_transform(k)) * local_aabb
		instances.append(_aabb_array(aabb))
	_multimeshes.append({"path": path, "instances": instances})

func _count_places(node: Node, path: String) -> void:
	var meta: Variant = node.get_meta("uzu_places")
	if not (meta is Dictionary):
		return
	var holder := String(node.name)
	for key in meta:
		var value: Variant = meta[key]
		if value is Dictionary and (value as Dictionary).has("position"):
			_places.append({"id": holder + "__" + String(key), "holder": holder,
				"position": _position_of(value)})
		elif value is Array:
			for item in value:
				if item is Dictionary and (item as Dictionary).has("name") and (item as Dictionary).has("position"):
					_places.append({"id": holder + "__" + String((item as Dictionary)["name"]),
						"holder": holder, "position": _position_of(item)})

func _position_of(d: Dictionary) -> Array:
	var p: Variant = d.get("position")
	if p is Array and (p as Array).size() >= 3:
		return [float(p[0]), float(p[1]), float(p[2])]
	if p is Vector3:
		var v := p as Vector3
		return [v.x, v.y, v.z]
	return []
