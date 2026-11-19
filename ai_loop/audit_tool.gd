extends SceneTree
## Re-read audit, engine side (BACKLOG B-1511): loads a SAVED scene
## (.tscn / .scn) back through the resource loader, instantiates it
## WITHOUT adding it to the tree (no _ready runs, nothing is played),
## and counts what is actually in the artifact: mesh triangles (index
## aware, per surface), textures (distinct, with dimensions), placed
## instances (nodes, MultiMesh instances as loaded, instanced scenes),
## collision shapes and bodies, lights and navigation meshes.
##
## Nothing here trusts the generator: the counts come only from the
## re-opened resources. The comparison against the generation spec set
## (B-1509) lives in ai_loop/audit_scene.py; this script only measures.
##
##   godot --headless --path <project> --script audit_tool.gd -- --scene res://x.tscn
##   -> prints one line: AUDIT_RESULT {json}
##
## Parts: the scene root's children are the parts (a generated map's
## holders, e.g. Land / Pond / Village); every count is reported per
## part and for the whole scene. Node paths are relative to the root.

var _textures: Dictionary = {} # key -> {key, class, width, height, path}

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
		print("AUDIT_RESULT ", JSON.stringify({"ok": false, "error": "needs --scene res://..."}))
		quit(1)
		return
	var packed: PackedScene = load(scene_path) as PackedScene
	if packed == null:
		print("AUDIT_RESULT ", JSON.stringify({"ok": false, "error": "could not load %s as a PackedScene" % scene_path}))
		quit(1)
		return
	var root := packed.instantiate()
	if root == null:
		print("AUDIT_RESULT ", JSON.stringify({"ok": false, "error": "could not instantiate %s" % scene_path}))
		quit(1)
		return
	var parts: Dictionary = {}
	var totals: Dictionary = _empty_counts()
	_count_node(root, ".", totals) # totals include the root; parts do not.
	_walk(root, ".", "", parts, totals)
	var textures: Array = []
	for key in _textures:
		textures.append(_textures[key])
	textures.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return String(a["key"]) < String(b["key"]))
	print("AUDIT_RESULT ", JSON.stringify({
		"ok": true, "scene": scene_path, "root": root.name,
		"totals": totals, "parts": parts, "textures": textures,
	}))
	root.free()
	quit()

func _empty_counts() -> Dictionary:
	return {
		"nodes": 0, "mesh_instances": 0, "multimesh_instances": 0,
		"multimesh_placements": 0, "scene_instances": 0,
		"triangles": 0, "mesh_triangles": 0, "multimesh_triangles": 0,
		"collision_shapes": 0, "collision_shapes_disabled": 0, "collision_bodies": 0,
		"lights": 0, "navigation_regions": 0,
		"mesh_nodes": [], "multimesh_nodes": [], "collision_shape_names": [],
		"navigation": [], "scene_instance_paths": [],
	}

func _mesh_triangles(mesh: Mesh) -> int:
	# Two counting routes, because each alone misses a case (both seen on
	# the generated village): a saved ArrayMesh in the compressed surface
	# format reports its lengths but surface_get_arrays comes back empty;
	# a PrimitiveMesh (BoxMesh, PlaneMesh) has no length methods at all
	# (they are ArrayMesh-only) and must be counted from its arrays.
	# Calling an ArrayMesh-only method on a PrimitiveMesh is a runtime
	# error that quietly returns 0, so the routes are chosen by type.
	var total := 0
	for s in mesh.get_surface_count():
		if mesh is ArrayMesh:
			var am := mesh as ArrayMesh
			var index_len: int = am.surface_get_array_index_len(s)
			var array_len: int = am.surface_get_array_len(s)
			total += (index_len / 3) if index_len > 0 else (array_len / 3)
		else:
			var arrays := mesh.surface_get_arrays(s)
			if arrays.size() == Mesh.ARRAY_MAX:
				var indices: Variant = arrays[Mesh.ARRAY_INDEX]
				var vertices: Variant = arrays[Mesh.ARRAY_VERTEX]
				if indices != null:
					total += (indices as PackedInt32Array).size() / 3
				elif vertices != null:
					total += (vertices as PackedVector3Array).size() / 3
	return total

func _collect_material(m: Material) -> void:
	if m == null:
		return
	if m is BaseMaterial3D:
		var bm := m as BaseMaterial3D
		for prop in bm.get_property_list():
			var pname: String = prop.get("name", "")
			if pname.ends_with("_texture") or pname == "albedo_texture":
				var value: Variant = bm.get(pname)
				if value is Texture2D:
					var t := value as Texture2D
					var key: String = t.resource_path if t.resource_path != "" else "instance:%d" % t.get_instance_id()
					if not _textures.has(key):
						_textures[key] = {"key": key, "class": t.get_class(),
							"width": t.get_width(), "height": t.get_height(), "path": t.resource_path}

func _collect_mesh_materials(mesh: Mesh) -> void:
	if mesh == null:
		return
	for s in mesh.get_surface_count():
		_collect_material(mesh.surface_get_material(s))

func _walk(node: Node, path: String, part: String, parts: Dictionary, totals: Dictionary) -> void:
	# path is "." for the root, else the root-relative path ("Land/Terrain").
	# part is "" for the root, else the top-level child's name.
	if path != ".":
		if not parts.has(part):
			parts[part] = _empty_counts()
		_count_node(node, path, parts[part])
		_count_node(node, path, totals)
	for child in node.get_children():
		var child_path: String = String(child.name) if path == "." else path + "/" + String(child.name)
		var child_part: String = String(child.name) if path == "." else part
		_walk(child, child_path, child_part, parts, totals)

func _count_node(node: Node, path: String, c: Dictionary) -> void:
	c["nodes"] = int(c["nodes"]) + 1
	if node is MeshInstance3D:
		var mi := node as MeshInstance3D
		c["mesh_instances"] = int(c["mesh_instances"]) + 1
		if mi.mesh != null:
			var tris := _mesh_triangles(mi.mesh)
			c["mesh_triangles"] = int(c["mesh_triangles"]) + tris
			c["triangles"] = int(c["triangles"]) + tris
			(c["mesh_nodes"] as Array).append({"path": path, "name": String(node.name), "triangles": tris})
			_collect_mesh_materials(mi.mesh)
		for s in mi.get_surface_override_material_count():
			_collect_material(mi.get_surface_override_material(s))
	if node is MultiMeshInstance3D:
		var mmi := node as MultiMeshInstance3D
		c["multimesh_instances"] = int(c["multimesh_instances"]) + 1
		if mmi.multimesh != null:
			var instances: int = mmi.multimesh.instance_count
			c["multimesh_placements"] = int(c["multimesh_placements"]) + instances
			var per := 0
			if mmi.multimesh.mesh != null:
				per = _mesh_triangles(mmi.multimesh.mesh)
				_collect_mesh_materials(mmi.multimesh.mesh)
			c["multimesh_triangles"] = int(c["multimesh_triangles"]) + per * instances
			c["triangles"] = int(c["triangles"]) + per * instances
			(c["multimesh_nodes"] as Array).append({"path": path, "name": String(node.name),
				"instances": instances, "per_instance_triangles": per})
	if node is CollisionShape3D:
		c["collision_shapes"] = int(c["collision_shapes"]) + 1
		if (node as CollisionShape3D).disabled:
			c["collision_shapes_disabled"] = int(c["collision_shapes_disabled"]) + 1
		(c["collision_shape_names"] as Array).append(String(node.name))
	if node is CollisionObject3D:
		c["collision_bodies"] = int(c["collision_bodies"]) + 1
	if node is Light3D:
		c["lights"] = int(c["lights"]) + 1
	if node is NavigationRegion3D:
		var nr := node as NavigationRegion3D
		c["navigation_regions"] = int(c["navigation_regions"]) + 1
		var polygons := 0
		var vertices := 0
		if nr.navigation_mesh != null:
			polygons = nr.navigation_mesh.get_polygon_count()
			vertices = nr.navigation_mesh.get_vertices().size()
		(c["navigation"] as Array).append({"path": path, "polygons": polygons, "vertices": vertices})
	if path != "." and node.scene_file_path != "":
		c["scene_instances"] = int(c["scene_instances"]) + 1
		(c["scene_instance_paths"] as Array).append({"path": path, "scene": node.scene_file_path})
