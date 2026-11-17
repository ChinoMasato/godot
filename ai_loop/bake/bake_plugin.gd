@tool
extends EditorPlugin
## Bakes lightmaps without the editor UI (BACKLOG B-805). ai_loop/uzu.py installs this
## plugin for one run of the editor, then removes it. Arguments after "--":
##   --uzu-bake-scene res://level.tscn   --uzu-bake-out result.json
##   [--uzu-bake-node Path/To/LightmapGI] [--uzu-bake-prepare]
## --uzu-bake-prepare turns on UV2 (add_uv2) for primitive meshes, which lightmaps need.
## Writes {"ok", "error", "light_data", "prepared", "meshes"} to the result file and quits.

const SETTLE_FRAMES := 30

var _frames := 0
var _done := false


func _enter_tree() -> void:
	set_process(true)


func _arg(name: String, default: String = "") -> String:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-bake-" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _process(_delta: float) -> void:
	if _done:
		return
	_frames += 1
	if _frames < SETTLE_FRAMES or EditorInterface.get_resource_filesystem().is_scanning():
		return
	_done = true
	var result := _bake()
	var out := _arg("out")
	if not out.is_empty():
		var f := FileAccess.open(out, FileAccess.WRITE)
		if f:
			f.store_string(JSON.stringify(result))
			f.close()
	get_tree().quit(0 if result.get("ok", false) else 1)


func _error_name(code: int) -> String:
	for name in ClassDB.class_get_enum_constants("LightmapGI", "BakeError"):
		if ClassDB.class_get_integer_constant("LightmapGI", name) == code:
			return name
	return str(code)


func _bake() -> Dictionary:
	var scene := _arg("scene")
	if scene.is_empty() or not ResourceLoader.exists(scene):
		return {"ok": false, "error": "no --scene '%s'" % scene}
	EditorInterface.open_scene_from_path(scene)
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.scene_file_path != scene:
		return {"ok": false, "error": "could not open '%s' in the editor" % scene}
	var node_path := _arg("node")
	var lightmap: LightmapGI = null
	if not node_path.is_empty():
		lightmap = root.get_node_or_null(node_path) as LightmapGI
	elif root is LightmapGI:
		lightmap = root
	else:
		var found := root.find_children("*", "LightmapGI", true, false)
		lightmap = found[0] as LightmapGI if found.size() > 0 else null
	if lightmap == null:
		return {"ok": false, "error": "no LightmapGI in the scene (or at --node)"}

	var prepared := 0
	var meshes := 0
	for mi in root.find_children("*", "MeshInstance3D", true, false):
		var mesh := (mi as MeshInstance3D).mesh
		if mesh == null:
			continue
		meshes += 1
		if OS.get_cmdline_user_args().has("--uzu-bake-prepare"):
			var primitive := mesh as PrimitiveMesh
			if primitive and not primitive.add_uv2:
				primitive.add_uv2 = true
				prepared += 1

	# Same target as the editor's Bake button: the LightmapGI's parent, or itself when it is the root.
	var from: Node = lightmap if lightmap == root else lightmap.get_parent()
	var started := Time.get_ticks_msec()
	var code: int = lightmap.bake(from, scene.get_basename() + ".lmbake")
	var result := {"ok": code == LightmapGI.BAKE_ERROR_OK, "error": _error_name(code), "scene": scene, "lightmap": str(root.get_path_to(lightmap)), "meshes": meshes, "prepared": prepared, "seconds": (Time.get_ticks_msec() - started) / 1000.0}
	if code == LightmapGI.BAKE_ERROR_OK:
		result["light_data"] = lightmap.light_data.resource_path if lightmap.light_data else ""
		var save_err := EditorInterface.save_scene()
		if save_err != OK:
			result["ok"] = false
			result["error"] = "baked, but could not save the scene: %s" % error_string(save_err)
	return result
