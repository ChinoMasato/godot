extends SceneTree
## Agent-facing operations on a project's scenes and on the engine's class
## database (BACKLOG B-401). Run through ai_loop/engine.sh, which also adds
## run, check and shot. Values use Godot's text format (var_to_str/str_to_var):
## 1.5, "text", Vector3(1, 2, 3), Color(1, 0, 0, 1), [1, 2], {"a": 1}.
##
## Scene commands (all take --scene res://path.tscn; node paths are relative to the root, "." is the root):
##   new        --type Node3D [--name Main]                      creates the scene file
##   tree       [--depth N]                                       nodes with type, script and child count
##   props      --node N                                          the properties shown in the inspector, with values
##   get        --node N --property P
##   set        --node N --property P --value V                   saves
##   add        --parent N --type T [--name X] [--set '{"position": "Vector3(0, 1, 0)"}']   saves
##   instance   --parent N --path res://other.tscn [--name X]     saves
##   remove     --node N                                          saves
##   rich       [--top 10]   static richness of the scene (B-1204): triangles, draw calls (one per surface),
##              objects, texture memory, props (MultiMesh instances and small meshes) per square meter
##              of the whole scene and of the area the props cover (props_per_m2_local),
##              lights; the heaviest nodes. ai_loop/uzu.py look rich adds a rendered frame and a budget
##   pbr-preset --name wood|stone|brick|dirt|grass|plaster|tile --out res://materials/pbr [--size 256] [--seed 1]
##              a procedural, seamless base texture for bare models (B-1209), turned into albedo/normal/ORM
##              PNGs with UZUPBRFromImage (import the project before pbr-assign)
##   pbr-assign --scene res://x.tscn --assign 'House*:plaster,Roof*:tile,Ground:grass' [--dir res://materials/pbr]
##              [--scale 0.5] [--no-tint]   gives matching nodes a triplanar PBR material of the preset, tinted by
##              their current color; saves
##   lod        [--node N] [--min-triangles 300] [--merge-angle 25] [--split-angle 60] [--imports]
##              distance LOD levels for the scene's meshes (B-1205): each mesh of a MeshInstance3D or a
##              MultiMesh with at least that many triangles gets simplified levels (meshoptimizer), which
##              the renderer picks by on-screen size; shared meshes are done once. --imports also turns
##              on LOD generation in the project's 3D model imports (.glb, .gltf, ...). saves
##   scatter    --surface Ground --mesh box|sphere|cylinder|capsule|prism|res://rock.tres|res://rock.glb
##              [--material res://m.tres] [--density 1] [--seed 1] [--scale 0.8,1.2] [--no-yaw] [--align 0]
##              [--max-slope 90] [--min-distance 0] [--sink 0] [--max-count 100000] [--name Scatter]
##              [--exclude '[{"center": [x, y, z], "radius": r}, {"aabb": [x, y, z, sx, sy, sz]}]']
##              props spread over a surface as one MultiMesh (B-1203); saves
##   reparent   --node N --parent P                               saves
##   call       --node N --method M [--args '[1, "a"]'] [--save]  returns the result
##   connect    --from N --signal S --to N --method M             persistent connection, saves
##   script     --node N --path res://a.gd                        attaches a script, saves
## Class database:
##   classes    [--inherits Node3D]
##   describe   --class Light3D                                   properties, methods, signals, constants
## Project:
##   validate                                                     loads every scene, resource and script; lists problems (B-203)
##   actions                                                      the project's input actions and their events (B-304)
##   map-import   --kind normal|depth --in res://model.png --for res://picture.png --out res://x.png
##                [--depth-kind auto|disparity|linear] [--no-align]   bring in an outside normal/depth map (B-903)
##   sfx          --out res://sfx/hit.wav [--preset hit] [--seed 3] [--set '{"decay": 0.3}']   a synthesized sound effect
##                (B-1105): a preset, varied by --seed, with parameters changed by --set (or all of them
##                without --preset); reports its length, peak, loudness and pitch so an agent can judge it
##   sfx-presets                                                  the presets and the parameters with their defaults
##   map                                                          static index of the project (B-204): files by kind, dependencies
##                                                                both ways, scenes (root, nodes, scripts, instances, signal
##                                                                connections), autoloads, global classes, main scene
## Resources, settings, input map and import settings (B-802):
##   res-new      --path res://m.tres --type StandardMaterial3D [--set '{"albedo_color": "Color(1, 0, 0, 1)"}']
##   res-props    --path res://m.tres                             inspector properties with values
##   res-get      --path res://m.tres --property P
##   res-set      --path res://m.tres --property P --value V       saves
##   setting-get  --name application/config/name
##   setting-set  --name display/window/size/viewport_width --value 1280   saves project.godot
##   action-add   --name jump [--key Space] [--mouse left] [--joy-button 0] [--deadzone 0.5] [--replace]
##   action-remove --name jump
##   import-get   --path res://icon.png                           the file's import settings (.import)
##   import-set   --path res://icon.png --param compress/mode --value 1   (uzu.py then reimports)
## Export (B-804):
##   export-preset --platform web|linux|windows|macos|android [--name Web] [--out build/web/index.html] [--template /path/release.zip]
##   presets                                                      the project's export presets
## Prints one line "ENGINE_RESULT {json}"; exits 0 on success, 1 on error.


func _initialize() -> void:
	var result: Dictionary
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		result = _error("no command; see ai_loop/engine.sh")
	else:
		result = _run(args[0], args)
	print("ENGINE_RESULT " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


func _option(args: PackedStringArray, name: String, default: String = "") -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _error(message: String) -> Dictionary:
	return {"ok": false, "error": message}


func _value_out(value: Variant) -> Variant:
	# JSON-friendly: plain types as they are, everything else in Godot's text format.
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return value
		TYPE_OBJECT:
			if value == null:
				return null
			var res := value as Resource
			if res and not res.resource_path.is_empty() and not res.resource_path.contains("::"):
				return {"resource": res.resource_path, "class": res.get_class()}
			if res:
				return {"class": res.get_class(), "embedded": true}
			return {"object": value.get_class()}
	return var_to_str(value)


func _value_in(text: String) -> Variant:
	# A resource path loads the resource; a resource class name creates one.
	if text.begins_with("res://") and ResourceLoader.exists(text):
		return load(text)
	if ClassDB.class_exists(text) and ClassDB.is_parent_class(text, "Resource") and ClassDB.can_instantiate(text):
		return ClassDB.instantiate(text)
	var value = str_to_var(text)
	# A bare word that is not a value (e.g. a name) is taken as a string.
	if value == null and text != "null":
		return text
	return value


func _node_info(root: Node, node: Node) -> Dictionary:
	var info := {"path": str(root.get_path_to(node)), "type": node.get_class(), "children": node.get_child_count()}
	var script := node.get_script() as Script
	if script:
		info["script"] = script.resource_path
	return info


func _load_scene(path: String) -> Node:
	var packed := load(path) as PackedScene if not path.is_empty() and ResourceLoader.exists(path) else null
	return packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE) if packed else null


func _save(root: Node, path: String) -> Error:
	var packed := PackedScene.new()
	var err := packed.pack(root)
	return ResourceSaver.save(packed, path) if err == OK else err


func _own_recursive(node: Node, owner: Node) -> void:
	for child in node.get_children():
		child.owner = owner
		if child.scene_file_path.is_empty():
			_own_recursive(child, owner)


func _run(command: String, args: PackedStringArray) -> Dictionary:
	match command:
		"validate":
			return _validate()
		"actions":
			return _actions()
		"map":
			return _map()
		"map-import":
			var kinds := {"normal": UZUMapImport.MAP_NORMAL, "depth": UZUMapImport.MAP_DEPTH}
			var depth_kinds := {"auto": UZUMapImport.DEPTH_AUTO, "disparity": UZUMapImport.DEPTH_DISPARITY, "linear": UZUMapImport.DEPTH_LINEAR}
			var kind := _option(args, "kind")
			var depth_kind := _option(args, "depth-kind", "auto")
			if not kinds.has(kind) or not depth_kinds.has(depth_kind):
				return _error("map-import needs --kind normal|depth (and --depth-kind auto|disparity|linear)")
			var result := UZUMapImport.import_file(_option(args, "in"), _option(args, "for"), _option(args, "out"), kinds[kind], depth_kinds[depth_kind], not args.has("--no-align"))
			result["command"] = command
			return result
		"sfx":
			var out := _option(args, "out")
			if out.is_empty() or not out.ends_with(".wav"):
				return _error("sfx needs --out res://path.wav")
			var params := {}
			var preset := _option(args, "preset")
			if not preset.is_empty():
				if not UZUSfx.get_preset_names().has(preset):
					return _error("unknown --preset '%s' (see sfx-presets)" % preset)
				params = UZUSfx.get_preset(preset, _option(args, "seed", "0").to_int())
			var changes = JSON.parse_string(_option(args, "set", "{}"))
			if not changes is Dictionary:
				return _error("--set needs a JSON object, e.g. '{\"decay\": 0.3}'")
			var defaults := UZUSfx.get_defaults()
			for key in changes:
				if not defaults.has(key):
					return _error("unknown parameter '%s' (see sfx-presets)" % key)
				params[key] = changes[key]
			if params.has("wave") and not ["square", "saw", "sine", "triangle", "noise"].has(params["wave"]):
				return _error("unknown wave '%s' (square, saw, sine, triangle or noise)" % params["wave"])
			var stream := UZUSfx.generate(params)
			if stream == null:
				return _error("could not make the sound (see the parameters)")
			DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out.get_base_dir()))
			var err := stream.save_to_wav(out)
			if err != OK:
				return _error("could not save %s (%s)" % [out, error_string(err)])
			return {"ok": true, "command": command, "out": out, "preset": preset, "params": params, "sound": UZUSfx.describe(stream)}
		"pbr-preset":
			return _pbr_preset(_option(args, "name"), _option(args, "out", "res://materials/pbr"), _option(args, "size", "256").to_int(), _option(args, "seed", "1").to_int())
		"sfx-presets":
			var presets := {}
			for name in UZUSfx.get_preset_names():
				presets[name] = UZUSfx.get_preset(name)
			return {"ok": true, "command": command, "presets": presets, "defaults": UZUSfx.get_defaults()}
		"res-new", "res-props", "res-get", "res-set":
			return _resource_command(command, args)
		"setting-get", "setting-set":
			return _setting_command(command, args)
		"action-add", "action-remove":
			return _action_command(command, args)
		"import-get", "import-set":
			return _import_command(command, args)
		"export-preset", "presets":
			return _preset_command(command, args)
		"classes":
			var base := _option(args, "inherits")
			var names: Array = Array(ClassDB.get_inheriters_from_class(base) if not base.is_empty() else ClassDB.get_class_list())
			names.sort()
			return {"ok": true, "command": command, "classes": names}
		"describe":
			var cls := _option(args, "class")
			if not ClassDB.class_exists(cls):
				return _error("unknown --class '%s'" % cls)
			var props := []
			for p in ClassDB.class_get_property_list(cls, true):
				if p.usage & PROPERTY_USAGE_EDITOR:
					props.append({"name": p.name, "type": type_string(p.type), "hint": p.hint_string})
			var methods := []
			for m in ClassDB.class_get_method_list(cls, true):
				if not m.name.begins_with("_"):
					methods.append({"name": m.name, "args": m.args.map(func(a): return a.name + ": " + type_string(a.type)), "return": type_string(m.return.type)})
			var signals := []
			for s in ClassDB.class_get_signal_list(cls, true):
				signals.append({"name": s.name, "args": s.args.map(func(a): return a.name)})
			var consts := {}
			for c in ClassDB.class_get_integer_constant_list(cls, true):
				consts[c] = ClassDB.class_get_integer_constant(cls, c)
			return {"ok": true, "command": command, "class": cls, "inherits": ClassDB.get_parent_class(cls), "properties": props, "methods": methods, "signals": signals, "constants": consts}
		"new":
			var path := _option(args, "scene")
			var type := _option(args, "type", "Node3D")
			if path.is_empty() or not ClassDB.can_instantiate(type) or not ClassDB.is_parent_class(type, "Node"):
				return _error("new needs --scene and a Node --type")
			var root: Node = ClassDB.instantiate(type)
			root.name = _option(args, "name", "Main")
			DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
			var err := _save(root, path)
			root.free()
			return {"ok": err == OK, "command": command, "scene": path, "error": error_string(err) if err != OK else ""}

	var scene_path := _option(args, "scene")
	var root := _load_scene(scene_path)
	if root == null:
		return _error("could not load --scene '%s'" % scene_path)
	var result := _scene_command(command, args, root)
	if result.get("ok", false) and result.get("save", false):
		var err := _save(root, scene_path)
		if err != OK:
			result = _error("could not save '%s': %s" % [scene_path, error_string(err)])
	result.erase("save")
	root.free()
	if result.get("ok", false):
		result["command"] = command
		result["scene"] = scene_path
	return result


## Triangles of a mesh (triangle surfaces only) and its surface count.
func _mesh_cost(mesh: Mesh) -> Vector2i:
	if mesh == null:
		return Vector2i.ZERO
	var tris := 0
	for i in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(i)
		if arrays.is_empty() or (mesh is ArrayMesh and mesh.surface_get_primitive_type(i) != Mesh.PRIMITIVE_TRIANGLES):
			continue  # Primitive meshes are always triangles.
		var indices = arrays[Mesh.ARRAY_INDEX]
		tris += (indices.size() if indices != null and indices.size() > 0 else arrays[Mesh.ARRAY_VERTEX].size()) / 3
	return Vector2i(tris, mesh.get_surface_count())


## The instances' extent, from the saved buffer (12 floats each, the origin in 3, 7, 11):
## a headless run has no renderer to ask.
func _multimesh_aabb(mm: MultiMesh, count: int) -> AABB:
	var buf := mm.buffer
	var stride := 12 + (4 if mm.use_colors else 0) + (4 if mm.use_custom_data else 0)
	var box := AABB()
	for i in mini(count, buf.size() / stride):
		var p := Vector3(buf[i * stride + 3], buf[i * stride + 7], buf[i * stride + 11])
		box = AABB(p, Vector3.ZERO) if i == 0 else box.expand(p)
	return box.grow(0.01) if count > 0 else AABB()


## The node's transform in the scene root's space (the scene is not in the tree).
func _to_root(root: Node, n: Node) -> Transform3D:
	var t := Transform3D()
	while n != null and n != root:
		if n is Node3D:
			t = n.transform * t
		n = n.get_parent()
	return t


func _visible(root: Node, n: Node) -> bool:
	while n != null:
		if (n is Node3D or n is CanvasItem) and not n.visible:
			return false
		if n == root:
			break
		n = n.get_parent()
	return true


func _texture_bytes(tex: Texture2D) -> int:
	var img := tex.get_image()
	if img == null:
		return tex.get_width() * tex.get_height() * 4
	var bytes := img.get_data_size()
	if not img.has_mipmaps() and tex is CompressedTexture2D:
		bytes = bytes * 4 / 3  # Imported textures usually get mipmaps on the GPU.
	return bytes


func _materials_of(n: Node) -> Array:
	var mats := []
	if n is GeometryInstance3D and n.material_override:
		mats.append(n.material_override)
	var mesh: Mesh = null
	if n is MeshInstance3D:
		mesh = n.mesh
		for i in n.get_surface_override_material_count():
			if n.get_surface_override_material(i):
				mats.append(n.get_surface_override_material(i))
	elif n is MultiMeshInstance3D and n.multimesh:
		mesh = n.multimesh.mesh
	elif n is GPUParticles3D:
		for i in n.draw_passes:
			if n.get_draw_pass_mesh(i):
				for k in n.get_draw_pass_mesh(i).get_surface_count():
					if n.get_draw_pass_mesh(i).surface_get_material(k):
						mats.append(n.get_draw_pass_mesh(i).surface_get_material(k))
	if mesh:
		for i in mesh.get_surface_count():
			if mesh.surface_get_material(i):
				mats.append(mesh.surface_get_material(i))
		if mesh is PrimitiveMesh and mesh.material:
			mats.append(mesh.material)
	if n is SpriteBase3D and n.texture:
		mats.append(n.texture)  # A texture stands for itself.
	return mats


func _textures_of(mat) -> Array:
	var out := []
	if mat is Texture2D:
		out.append(mat)
	elif mat is BaseMaterial3D:
		for i in BaseMaterial3D.TEXTURE_MAX:
			if mat.get_texture(i):
				out.append(mat.get_texture(i))
	elif mat is ShaderMaterial:
		for p in mat.get_property_list():
			if p.name.begins_with("shader_parameter/") and mat.get(p.name) is Texture2D:
				out.append(mat.get(p.name))
	return out


## Model quality (B-1219): how shaped a mesh is. Edges shared by two faces are sorted by the
## angle between them: 30-60 degrees a chamfer or bevel, above 75 sharp (the rest are curves). A
## plain box has only sharp edges; a modelled part has chamfers. Big meshes are sampled
## (the first QUALITY_SAMPLE triangles).
const QUALITY_SAMPLE := 20000
var _quality_cache := {}


func _model_quality(mesh: Mesh) -> Dictionary:
	if _quality_cache.has(mesh):
		return _quality_cache[mesh]
	var tris := 0
	var sampled := 0
	var chamfer := 0
	var sharp := 0
	var area := 0.0
	var dirs := {}
	var open := {}
	for s in mesh.get_surface_count():
		if mesh is ArrayMesh and mesh.surface_get_primitive_type(s) != Mesh.PRIMITIVE_TRIANGLES:
			continue
		var arr := mesh.surface_get_arrays(s)
		var v: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
		var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX] if arr[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
		var count := idx.size() / 3 if idx.size() > 0 else v.size() / 3
		tris += count
		for t in mini(count, maxi(0, QUALITY_SAMPLE - sampled)):
			var ia := idx[t * 3] if idx.size() > 0 else t * 3
			var ib := idx[t * 3 + 1] if idx.size() > 0 else t * 3 + 1
			var ic := idx[t * 3 + 2] if idx.size() > 0 else t * 3 + 2
			var a := v[ia]
			var b := v[ib]
			var c := v[ic]
			var n := (b - a).cross(c - a)
			if n.length_squared() < 1e-12:
				continue
			area += n.length() * 0.5
			n = n.normalized()
			dirs[Vector3i(roundi(n.x * 4), roundi(n.y * 4), roundi(n.z * 4))] = true
			for e in [[a, b], [b, c], [c, a]]:
				var p := Vector3i((e[0] as Vector3) * 1000.0)
				var q := Vector3i((e[1] as Vector3) * 1000.0)
				var key := "%d,%d,%d|%d,%d,%d" % ([p.x, p.y, p.z, q.x, q.y, q.z] if p < q else [q.x, q.y, q.z, p.x, p.y, p.z])
				if open.has(key):
					var deg := rad_to_deg(acos(clampf(n.dot(open[key]), -1.0, 1.0)))
					open.erase(key)
					if deg > 75.0:
						sharp += 1
					elif deg > 30.0 and deg < 60.0:
						chamfer += 1  # 45-degree cuts; gentler bends are curved surfaces (foliage, rocks)
				else:
					open[key] = n
			sampled += 1
	var out := {"triangles": tris, "sampled": sampled, "chamfered_edges": chamfer, "sharp_edges": sharp,
		"bevel_share": snappedf(float(chamfer) / (chamfer + sharp), 0.001) if chamfer + sharp > 0 else 0.0,
		"normal_directions": dirs.size(),
		"triangles_per_m2": snappedf(sampled / area, 0.01) if area > 0 else 0.0,
		"size_m": snappedf(mesh.get_aabb().get_longest_axis_size(), 0.01)}
	_quality_cache[mesh] = out
	return out


func _sorted_by(items: Array, key: String) -> Array:
	var out := items.duplicate()
	out.sort_custom(func(a, b): return a[key] > b[key])
	return out


## Whether a material carries a PBR texture set: color and normal maps at least.
func _material_pbr(mat) -> Dictionary:
	var textured := false
	var normal := false
	if mat is BaseMaterial3D:
		textured = mat.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) != null
		normal = mat.normal_enabled and mat.get_texture(BaseMaterial3D.TEXTURE_NORMAL) != null
	elif mat is ShaderMaterial:
		for p in mat.get_property_list():
			if p.name.begins_with("shader_parameter/") and mat.get(p.name) is Texture2D:
				textured = true
				normal = normal or "normal" in p.name.to_lower()
	return {"textured": textured, "normal": normal}


func _rich(root: Node, top_n: int) -> Dictionary:
	var nodes := []
	var tris := 0
	var calls := 0
	var objects := 0
	var props := 0
	var lights := 0
	var shadow_lights := 0
	var bounds := AABB()
	var have_bounds := false
	var prop_bounds := AABB()
	var have_prop_bounds := false
	var textures := {}
	var by_type := {}
	var q_tris := 0
	var q_textured := 0
	var q_pbr := 0
	var q_chamfer := 0
	var q_sharp := 0
	var q_sampled := 0
	var q_area := 0.0
	var q_cheap := []
	var q_meshes := []
	var q_curved := 0
	for n in [root] + root.find_children("*", "", true, false):
		if not _visible(root, n):
			continue
		if n is Light3D:
			lights += 1
			shadow_lights += int(n.shadow_enabled)
			continue
		if not n is GeometryInstance3D:
			continue
		var t := 0
		var c := 0
		var count := 1
		var aabb := AABB()
		if n is MeshInstance3D and n.mesh:
			var cost := _mesh_cost(n.mesh)
			t = cost.x
			c = cost.y
			aabb = n.mesh.get_aabb()
			if (_to_root(root, n) * aabb).get_longest_axis_size() < 1.0:
				props += 1  # A small mesh on its own is a prop too (small as placed, after scaling).
		elif n is MultiMeshInstance3D and n.multimesh and n.multimesh.mesh:
			count = n.multimesh.visible_instance_count if n.multimesh.visible_instance_count >= 0 else n.multimesh.instance_count
			var cost := _mesh_cost(n.multimesh.mesh)
			t = cost.x * count
			c = cost.y
			props += count
			aabb = _multimesh_aabb(n.multimesh, count)
		elif n is GPUParticles3D or n is CPUParticles3D:
			count = n.amount
			var pmesh: Mesh = n.get_draw_pass_mesh(0) if n is GPUParticles3D else n.mesh
			var cost := _mesh_cost(pmesh)
			t = cost.x * count
			c = maxi(1, cost.y)
		elif n is SpriteBase3D or n is Label3D:
			t = 2
			c = 1
		else:
			continue
		tris += t
		calls += c
		objects += 1
		var kind: String = n.get_class()
		by_type[kind] = by_type.get(kind, {"nodes": 0, "triangles": 0, "draw_calls": 0})
		by_type[kind].nodes += 1
		by_type[kind].triangles += t
		by_type[kind].draw_calls += c
		var lod_mesh: Mesh = n.mesh if n is MeshInstance3D else (n.multimesh.mesh if n is MultiMeshInstance3D else null)
		var lods: int = _lod_levels(lod_mesh).size() - 1 if lod_mesh else 0
		nodes.append({"node": str(root.get_path_to(n)), "type": kind, "triangles": t, "draw_calls": c, "instances": count, "lods": lods})
		if lod_mesh:
			var mq := _model_quality(lod_mesh)
			var pbr := {"textured": false, "normal": false}
			for mat in _materials_of(n):
				var mp := _material_pbr(mat)
				pbr.textured = pbr.textured or mp.textured
				pbr.normal = pbr.normal or mp.normal
			q_tris += t
			q_textured += t if pbr.textured else 0
			q_pbr += t if pbr.textured and pbr.normal else 0
			if mq.normal_directions <= 64:  # Hard surfaces; curved ones (foliage, rocks) say nothing about bevels.
				q_chamfer += mq.chamfered_edges * count
				q_sharp += mq.sharp_edges * count
			else:
				q_curved += 1
			q_sampled += mq.sampled * count
			q_area += (mq.sampled / mq.triangles_per_m2 if mq.triangles_per_m2 > 0 else 0.0) * count
			# Cheap: a big shape that is a plain box (few faces, no chamfer) with no texture.
			if mq.size_m > 1.5 and mq.triangles <= 24 and mq.chamfered_edges == 0 and not pbr.textured:
				q_cheap.append(str(root.get_path_to(n)))
			if mq.chamfered_edges + mq.sharp_edges >= 24:
				q_meshes.append({"node": str(root.get_path_to(n)), "triangles": t, "bevel_share": mq.bevel_share, "sharp_edges": mq.sharp_edges * count,
					"normal_directions": mq.normal_directions, "pbr": pbr.textured and pbr.normal})
		if aabb.size != Vector3.ZERO:
			var world := _to_root(root, n) * aabb
			bounds = world if not have_bounds else bounds.merge(world)
			have_bounds = true
			if n is MultiMeshInstance3D or world.get_longest_axis_size() < 1.0:
				prop_bounds = world if not have_prop_bounds else prop_bounds.merge(world)
				have_prop_bounds = true
		for mat in _materials_of(n):
			for tex in _textures_of(mat):
				textures[tex] = true
	# Materials without any texture look cheap (B-1209): their share among the scene's materials.
	var untextured := 0
	var material_count := 0
	var seen_mats := {}
	for n in [root] + root.find_children("*", "GeometryInstance3D", true, false):
		if not n is GeometryInstance3D or not _visible(root, n) or n is GPUParticles3D or n is CPUParticles3D:
			continue
		var mats := _materials_of(n).filter(func(m): return m is Material)
		if mats.is_empty() and (n is MeshInstance3D or n is MultiMeshInstance3D):
			untextured += 1  # The default grey material.
			material_count += 1
		for m in mats:
			if seen_mats.has(m):
				continue
			seen_mats[m] = true
			material_count += 1
			if _textures_of(m).is_empty():
				untextured += 1
	var texture_bytes := 0
	for tex in textures:
		texture_bytes += _texture_bytes(tex)
	nodes.sort_custom(func(a, b): return a.triangles > b.triangles)
	var area := bounds.size.x * bounds.size.z if have_bounds else 0.0
	# Where the props are: a huge ground plane would otherwise make any density look like zero.
	var prop_area := prop_bounds.size.x * prop_bounds.size.z if have_prop_bounds else 0.0
	return {"ok": true, "summary": {
			"triangles": tris, "draw_calls": calls, "objects": objects, "textures": textures.size(),
			"texture_mb": snappedf(texture_bytes / 1048576.0, 0.01), "props": props, "area_m2": snappedf(area, 0.01),
			"props_per_m2": snappedf(props / area, 0.001) if area > 0 else 0.0,
			"prop_area_m2": snappedf(prop_area, 0.01), "props_per_m2_local": snappedf(props / prop_area, 0.001) if prop_area > 0 else 0.0,
			"lights": lights, "shadow_lights": shadow_lights,
			"materials": material_count, "untextured_share": snappedf(float(untextured) / material_count, 0.01) if material_count > 0 else 0.0},
		"by_type": by_type, "top": nodes.slice(0, maxi(top_n, 0)),
		"model_quality": {
			"pbr_coverage": snappedf(float(q_pbr) / q_tris, 0.001) if q_tris > 0 else 0.0,
			"textured_share": snappedf(float(q_textured) / q_tris, 0.001) if q_tris > 0 else 0.0,
			"bevel_share": snappedf(float(q_chamfer) / (q_chamfer + q_sharp), 0.001) if q_chamfer + q_sharp > 0 else 0.0,
			"chamfered_edges": q_chamfer, "sharp_edges": q_sharp, "curved_meshes": q_curved,
			"triangles_per_m2": snappedf(q_sampled / q_area, 0.01) if q_area > 0 else 0.0,
			"cheap_meshes": q_cheap.size(), "cheap": q_cheap.slice(0, 8),
			"sharpest": _sorted_by(q_meshes, "sharp_edges").slice(0, 6),
			"about": "pbr_coverage: share of triangles whose material has color and normal maps; bevel_share: chamfered edges (30-60 degrees) among chamfered and sharp (>75) ones, over hard-surface meshes (curved_meshes, with more than 64 face directions, are left out); sharpest: the meshes with the most sharp edges (low bevel_share there reads as primitives); triangles_per_m2: over the meshes' surface; cheap: big plain boxes without texture (sampled, first %d triangles of each mesh)" % QUALITY_SAMPLE},
		"note": "static estimate: draw calls are one per surface before culling and shadows; look rich adds a rendered frame"}


const PBR_PRESETS := {
	# name: [UZUPBRFromImage kind, base tone]. Tones stay light and close to neutral, so a
	# model's own color can tint them (a red roof stays red, with texture).
	"wood": ["wood", Color(0.78, 0.66, 0.52)], "stone": ["cobble", Color(0.78, 0.76, 0.72)],
	"brick": ["brick", Color(0.82, 0.68, 0.6)], "dirt": ["ground", Color(0.74, 0.64, 0.5)],
	"grass": ["ground", Color(0.7, 0.8, 0.6)], "plaster": ["plaster", Color(0.92, 0.9, 0.86)],
	"tile": ["brick", Color(0.85, 0.75, 0.7)],
}


func _noise(type: int, freq: float, seed: int, size: int, octaves := 4) -> PackedFloat32Array:
	var n := FastNoiseLite.new()
	n.noise_type = type
	n.frequency = freq
	n.seed = seed
	n.fractal_octaves = octaves
	var img := n.get_seamless_image(size, size)
	var out := PackedFloat32Array()
	out.resize(size * size)
	for y in size:
		for x in size:
			out[y * size + x] = img.get_pixel(x, y).r
	return out


## A seamless base texture, drawn from noise and simple patterns (no outside images).
func _pbr_base(name: String, size: int, seed: int) -> Image:
	var tone: Color = PBR_PRESETS[name][1]
	var img := Image.create_empty(size, size, false, Image.FORMAT_RGB8)
	var soft := _noise(FastNoiseLite.TYPE_PERLIN, 4.0 / size, seed, size)
	var fine := _noise(FastNoiseLite.TYPE_VALUE, 32.0 / size, seed + 1, size, 2)
	var cells := _noise(FastNoiseLite.TYPE_CELLULAR, 6.0 / size, seed + 2, size, 1) if name == "stone" else PackedFloat32Array()
	for y in size:
		for x in size:
			var i := y * size + x
			var v := 0.85 + 0.15 * soft[i]
			match name:
				"wood":
					# Planks across, grain along them, a dark seam between planks.
					var plank := size / 4
					var grain := 0.5 + 0.5 * sin((x * 0.15 + soft[i] * 14.0 + floori(y / plank) * 2.3))
					v = 0.72 + 0.2 * grain + 0.08 * fine[i]
					if y % plank < 2:
						v *= 0.55
				"stone":
					v = 0.7 + 0.25 * soft[i] + 0.05 * fine[i]
					if cells[i] > 0.72:  # The mortar between cobbles.
						v *= 0.55
				"brick", "tile":
					var bh := size / (8 if name == "brick" else 6)
					var bw := size / (4 if name == "brick" else 6)
					var row := floori(y / bh)
					var bx := (x + (bw / 2 if row % 2 == 1 else 0)) % bw
					v = 0.78 + 0.12 * soft[i] + 0.1 * fine[i]
					if y % bh < 2 or bx < 2:
						v = 0.5 + 0.1 * fine[i]
					elif name == "tile":
						v *= 0.8 + 0.2 * float(y % bh) / bh  # Overlapping shingles, darker at the top.
				"dirt":
					v = 0.65 + 0.25 * soft[i] + 0.1 * fine[i]
					if fine[i] > 0.93:
						v = 0.95  # Pebbles.
				"grass":
					var blade := 0.5 + 0.5 * sin(x * 1.3 + fine[i] * 6.0)
					v = 0.62 + 0.2 * soft[i] + 0.18 * blade * fine[i]
				"plaster":
					v = 0.86 + 0.1 * soft[i] + 0.04 * fine[i]
			img.set_pixel(x, y, Color(tone.r * v, tone.g * v, tone.b * v))
	return img


func _pbr_preset(name: String, out: String, size: int, seed: int) -> Dictionary:
	if not PBR_PRESETS.has(name):
		return _error("pbr-preset needs --name %s" % "|".join(PBR_PRESETS.keys()))
	var base := _pbr_base(name, clampi(size, 64, 1024), seed)
	var made: Dictionary = UZUPBRFromImage.make(base, PBR_PRESETS[name][0], 1.0, 0.0, true)
	if not made.get("ok", false):
		return _error(str(made.get("error", "UZUPBRFromImage failed")))
	var dir := out.path_join(name)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(dir))
	var files := {}
	for key in ["albedo", "normal", "orm"]:
		var path := dir.path_join(key + ".png")
		(made[key] as Image).save_png(path)
		files[key] = path
	return {"ok": true, "command": "pbr-preset", "name": name, "kind": PBR_PRESETS[name][0], "files": files, "stats": made.get("stats", {})}


## Material color of a node now (to tint the preset), or white.
func _current_color(n: Node) -> Color:
	for mat in _materials_of(n):
		if mat is BaseMaterial3D:
			return mat.albedo_color
	return Color.WHITE


func _pbr_assign(root: Node, args: PackedStringArray) -> Dictionary:
	var dir := _option(args, "dir", "res://materials/pbr")
	var scale := _option(args, "scale", "0.5").to_float()
	var tint := not args.has("--no-tint")
	var rules := []
	for part in _option(args, "assign").split(",", false):
		var kv := part.split(":")
		if kv.size() != 2 or not PBR_PRESETS.has(kv[1].strip_edges()):
			return _error("--assign needs 'NodePattern:preset,...' with presets %s" % ", ".join(PBR_PRESETS.keys()))
		rules.append([kv[0].strip_edges(), kv[1].strip_edges()])
	if rules.is_empty():
		return _error("pbr-assign needs --assign 'House*:plaster,Ground:grass'")
	var assigned := []
	var cache := {}
	for n in root.find_children("*", "GeometryInstance3D", true, false):
		if not (n is MeshInstance3D or n is MultiMeshInstance3D):
			continue
		for rule in rules:
			if not String(n.name).match(rule[0]):
				continue
			var preset: String = rule[1]
			if not cache.has(preset):
				var tex := {}
				for key in ["albedo", "normal", "orm"]:
					var path := dir.path_join(preset).path_join(key + ".png")
					var t = load(path) if ResourceLoader.exists(path) else null
					if not t is Texture2D:
						return _error("no imported %s; run pbr-preset --name %s and import first (uzu.py look pbr does both)" % [path, preset])
					tex[key] = t
				cache[preset] = tex
			var mat := ORMMaterial3D.new()
			mat.albedo_texture = cache[preset].albedo
			mat.normal_enabled = true
			mat.normal_texture = cache[preset].normal
			mat.orm_texture = cache[preset].orm
			# Triplanar in world space: boxes and planes of any size get the same texel scale.
			mat.uv1_triplanar = true
			mat.uv1_world_triplanar = true
			mat.uv1_scale = Vector3(scale, scale, scale)
			if tint:
				var c := _current_color(n)
				mat.albedo_color = Color(minf(c.r * 1.25, 1.0), minf(c.g * 1.25, 1.0), minf(c.b * 1.25, 1.0))
			n.material_override = mat
			assigned.append({"node": str(root.get_path_to(n)), "preset": preset})
			break
	return {"ok": not assigned.is_empty(), "save": not assigned.is_empty(), "assigned": assigned, "error": "" if not assigned.is_empty() else "no node matched --assign"}


## Triangles per LOD level of a mesh's surfaces (level 0 is the full mesh).
func _lod_levels(mesh: Mesh) -> Array:
	var levels := []
	for i in mesh.get_surface_count():
		var surf := RenderingServer.mesh_get_surface(mesh.get_rid(), i)
		var bytes_per_index := 4 if surf.get("vertex_count", 0) > 65535 else 2
		var tris := [int(surf.get("index_count", 0)) / 3 if surf.get("index_count", 0) > 0 else int(surf.get("vertex_count", 0)) / 3]
		for l in surf.get("lods", []):
			tris.append(l.index_data.size() / bytes_per_index / 3)
		for k in tris.size():
			if k >= levels.size():
				levels.append(0)
			levels[k] += tris[k]
	return levels


func _lod(root: Node, args: PackedStringArray) -> Dictionary:
	var min_tris := _option(args, "min-triangles", "300").to_int()
	var merge := _option(args, "merge-angle", "25").to_float()
	var split := _option(args, "split-angle", "60").to_float()
	var only := _option(args, "node")
	var nodes := [root.get_node_or_null(only)] if not only.is_empty() else [root] + root.find_children("*", "", true, false)
	if nodes[0] == null:
		return _error("no --node '%s'" % only)
	var done := {}  # Old mesh -> new mesh, so shared meshes are simplified once.
	var meshes := []
	var skipped := []
	for n in nodes:
		var mesh: Mesh = null
		if n is MeshInstance3D:
			mesh = n.mesh
		elif n is MultiMeshInstance3D and n.multimesh:
			mesh = n.multimesh.mesh
		if mesh == null:
			continue
		if not done.has(mesh):
			var tris: int = _mesh_cost(mesh).x
			var before := _lod_levels(mesh)
			if tris < min_tris or before.size() > 1:
				skipped.append({"node": str(root.get_path_to(n)), "triangles": tris, "reason": "has LODs already" if before.size() > 1 else "fewer than %d triangles" % min_tris})
				done[mesh] = mesh
				continue
			var im := ImporterMesh.from_mesh(mesh)
			im.generate_lods(merge, split, [])
			var lodded: ArrayMesh = im.get_mesh()
			lodded.resource_name = mesh.resource_name
			done[mesh] = lodded
			meshes.append({"node": str(root.get_path_to(n)), "triangles": tris, "levels": _lod_levels(lodded)})
		if done[mesh] != mesh:
			if n is MeshInstance3D:
				n.mesh = done[mesh]
			else:
				n.multimesh.mesh = done[mesh]
	var result := {"ok": true, "save": not meshes.is_empty(), "meshes": meshes, "skipped": skipped}
	if args.has("--imports"):
		result["imports"] = _lod_imports()
	return result


## Turns on LOD generation in the project's 3D model imports (on by default; some turn it off).
func _lod_imports() -> Array:
	var changed := []
	var stack := ["res://"]
	while not stack.is_empty():
		var dir: String = stack.pop_back()
		for d in DirAccess.get_directories_at(dir):
			if not d.begins_with("."):
				stack.append(dir.path_join(d))
		for f in DirAccess.get_files_at(dir):
			if not f.ends_with(".import"):
				continue
			var cfg := ConfigFile.new()
			var path := dir.path_join(f)
			if cfg.load(path) != OK or cfg.get_value("remap", "importer", "") != "scene":
				continue
			if not cfg.get_value("params", "meshes/generate_lods", true):
				cfg.set_value("params", "meshes/generate_lods", true)
				cfg.save(path)
				changed.append(path.trim_suffix(".import"))
	return changed


## The mesh to scatter: a primitive by name, a Mesh resource, or the first mesh of a scene (GLB).
func _scatter_mesh(spec: String) -> Mesh:
	var primitives := {"box": BoxMesh, "sphere": SphereMesh, "cylinder": CylinderMesh, "capsule": CapsuleMesh, "prism": PrismMesh}
	if primitives.has(spec):
		return primitives[spec].new()
	if spec.is_empty() or not ResourceLoader.exists(spec):
		return null
	var res := load(spec)
	if res is Mesh:
		return res
	if res is PackedScene:
		var inst: Node = res.instantiate()
		var found: Mesh = null
		for n in [inst] + inst.find_children("*", "MeshInstance3D", true, false):
			if n is MeshInstance3D and n.mesh != null:
				found = n.mesh
				break
		inst.free()
		return found
	return null


func _scene_command(command: String, args: PackedStringArray, root: Node) -> Dictionary:
	var node := root.get_node_or_null(_option(args, "node", "."))
	match command:
		"tree":
			var depth := _option(args, "depth", "-1").to_int()
			var nodes := [_node_info(root, root)]
			for n in root.find_children("*", "", true, false):
				if depth < 0 or str(root.get_path_to(n)).count("/") < depth:
					nodes.append(_node_info(root, n))
			return {"ok": true, "nodes": nodes}
		"props":
			if node == null:
				return _error("no --node '%s'" % _option(args, "node"))
			var values := {}
			for p in node.get_property_list():
				if p.usage & PROPERTY_USAGE_EDITOR and not p.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP):
					values[p.name] = _value_out(node.get(p.name))
			return {"ok": true, "node": _node_info(root, node), "properties": values}
		"get":
			var prop := _option(args, "property")
			if node == null or not prop in node:
				return _error("no --node or --property '%s'" % prop)
			return {"ok": true, "node": str(root.get_path_to(node)), "property": prop, "value": _value_out(node.get_indexed(prop))}
		"set":
			var prop := _option(args, "property")
			if node == null or not prop.get_slice(":", 0) in node:
				return _error("no --node or --property '%s'" % prop)
			node.set_indexed(prop, _value_in(_option(args, "value")))
			return {"ok": true, "save": true, "node": str(root.get_path_to(node)), "property": prop, "value": _value_out(node.get_indexed(prop))}
		"add":
			var parent := root.get_node_or_null(_option(args, "parent", "."))
			var type := _option(args, "type")
			if parent == null or not ClassDB.can_instantiate(type) or not ClassDB.is_parent_class(type, "Node"):
				return _error("add needs an existing --parent and a Node --type")
			var child: Node = ClassDB.instantiate(type)
			child.name = _option(args, "name", type)
			parent.add_child(child, true)
			child.owner = root
			var props = JSON.parse_string(_option(args, "set", "{}"))
			if typeof(props) == TYPE_DICTIONARY:
				for key in props:
					child.set_indexed(key, _value_in(str(props[key])) if typeof(props[key]) == TYPE_STRING else props[key])
			return {"ok": true, "save": true, "node": _node_info(root, child)}
		"rich":
			return _rich(root, _option(args, "top", "10").to_int())
		"lod":
			return _lod(root, args)
		"pbr-assign":
			return _pbr_assign(root, args)
		"scatter":
			var surface := root.get_node_or_null(_option(args, "surface")) as MeshInstance3D
			if surface == null or surface.mesh == null:
				return _error("scatter needs --surface, a MeshInstance3D with a mesh")
			var mesh := _scatter_mesh(_option(args, "mesh"))
			if mesh == null:
				return _error("scatter needs --mesh box|sphere|cylinder|capsule|prism or a res:// mesh or scene")
			if not _option(args, "material").is_empty():
				var material := load(_option(args, "material")) as Material if ResourceLoader.exists(_option(args, "material")) else null
				if material == null:
					return _error("no --material '%s'" % _option(args, "material"))
				mesh = mesh.duplicate()
				if mesh is PrimitiveMesh:
					mesh.material = material
				else:
					mesh.surface_set_material(0, material)
			var options := {"random_yaw": not args.has("--no-yaw")}
			var numbers := {"density": "density", "align": "align", "max-slope": "max_slope", "min-distance": "min_distance", "sink": "sink"}
			for flag in numbers:
				if not _option(args, flag).is_empty():
					options[numbers[flag]] = _option(args, flag).to_float()
			for flag in ["seed", "max-count"]:
				if not _option(args, flag).is_empty():
					options[flag.replace("-", "_")] = _option(args, flag).to_int()
			var scale := _option(args, "scale").split(",")
			if scale.size() == 2:
				options["scale_min"] = scale[0].to_float()
				options["scale_max"] = scale[1].to_float()
			var zones = JSON.parse_string(_option(args, "exclude", "[]"))
			if not zones is Array:
				return _error("--exclude needs a JSON array of {center, radius} or {aabb} zones")
			var exclude := []
			for z in zones:
				if z is Dictionary and z.has("center"):
					exclude.append({"center": Vector3(z.center[0], z.center[1], z.center[2]), "radius": float(z.get("radius", 1.0))})
				elif z is Dictionary and z.has("aabb"):
					exclude.append({"aabb": AABB(Vector3(z.aabb[0], z.aabb[1], z.aabb[2]), Vector3(z.aabb[3], z.aabb[4], z.aabb[5]))})
			options["exclude"] = exclude
			var result := UZUScatter.scatter(root, surface, mesh, options, _option(args, "name", "Scatter"))
			if not result.ok:
				return _error(result.error)
			var bounds: AABB = result.bounds
			result["bounds"] = {"position": [bounds.position.x, bounds.position.y, bounds.position.z], "size": [bounds.size.x, bounds.size.y, bounds.size.z]}
			result["save"] = true
			return result
		"instance":
			var parent := root.get_node_or_null(_option(args, "parent", "."))
			var packed := load(_option(args, "path")) as PackedScene if ResourceLoader.exists(_option(args, "path")) else null
			if parent == null or packed == null:
				return _error("instance needs an existing --parent and --path to a scene")
			var inst := packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
			if not _option(args, "name").is_empty():
				inst.name = _option(args, "name")
			parent.add_child(inst, true)
			inst.owner = root
			return {"ok": true, "save": true, "node": _node_info(root, inst)}
		"remove":
			if node == null or node == root:
				return _error("remove needs a --node that is not the root")
			var removed := str(root.get_path_to(node))
			node.get_parent().remove_child(node)
			node.free()
			return {"ok": true, "save": true, "removed": removed}
		"reparent":
			var parent := root.get_node_or_null(_option(args, "parent"))
			if node == null or node == root or parent == null or node.is_ancestor_of(parent):
				return _error("reparent needs a --node (not the root) and a --parent outside it")
			# The edited scene is not in the tree, so keep the local transform.
			node.reparent(parent, false)
			node.owner = root
			_own_recursive(node, root)
			return {"ok": true, "save": true, "node": _node_info(root, node)}
		"call":
			var method := _option(args, "method")
			if node == null or not node.has_method(method):
				return _error("no --node or --method '%s'" % method)
			var call_args = str_to_var(_option(args, "args", "[]"))
			if typeof(call_args) != TYPE_ARRAY:
				return _error("--args must be an array, e.g. '[1, \"a\"]'")
			var ret = node.callv(method, call_args)
			return {"ok": true, "save": args.has("--save"), "node": str(root.get_path_to(node)), "method": method, "result": _value_out(ret)}
		"connect":
			var from := root.get_node_or_null(_option(args, "from"))
			var to := root.get_node_or_null(_option(args, "to"))
			var sig := _option(args, "signal")
			var method := _option(args, "method")
			if from == null or to == null or not from.has_signal(sig) or method.is_empty():
				return _error("connect needs --from, --signal (existing), --to and --method")
			var err := from.connect(sig, Callable(to, method), CONNECT_PERSIST)
			return {"ok": err == OK, "save": true, "from": str(root.get_path_to(from)), "signal": sig, "to": str(root.get_path_to(to)), "method": method}
		"script":
			var script := load(_option(args, "path")) as Script if ResourceLoader.exists(_option(args, "path")) else null
			if node == null or script == null:
				return _error("script needs a --node and --path to a script")
			node.set_script(script)
			return {"ok": true, "save": true, "node": _node_info(root, node)}
	return _error("unknown command '%s'" % command)


const RESOURCE_EXTENSIONS := ["tscn", "scn", "tres", "res"]
const SCRIPT_EXTENSIONS := ["gd"]


# Every file under res:// except hidden directories (.godot, .git...).
func _project_files(dir: String, out: Array) -> void:
	for sub in DirAccess.get_directories_at(dir):
		if not sub.begins_with("."):
			_project_files(dir.path_join(sub), out)
	for file in DirAccess.get_files_at(dir):
		out.append(dir.path_join(file))


# A dependency entry is "uid://...::Type::res://path" or a plain path.
func _dependency_path(entry: String) -> String:
	var parts := entry.split("::")
	var path := parts[parts.size() - 1] if parts.size() >= 3 else parts[0]
	if path.is_empty() or path.begins_with("uid://"):
		var uid := ResourceUID.text_to_id(parts[0])
		if uid != ResourceUID.INVALID_ID and ResourceUID.has_id(uid):
			path = ResourceUID.get_id_path(uid)
	return path


func _exists(path: String) -> bool:
	return ResourceLoader.exists(path) or FileAccess.file_exists(path)


func _validate() -> Dictionary:
	var issues := []
	var files := []
	_project_files("res://", files)
	var checked := 0
	for file in files:
		var ext: String = file.get_extension().to_lower()
		if ext in RESOURCE_EXTENSIONS:
			checked += 1
			for entry in ResourceLoader.get_dependencies(file):
				var dep := _dependency_path(entry)
				if not _exists(dep):
					issues.append({"kind": "missing_dependency", "file": file, "target": dep, "message": "%s refers to %s, which does not exist" % [file, dep]})
			if ResourceLoader.load(file, "", ResourceLoader.CACHE_MODE_IGNORE) == null:
				issues.append({"kind": "load_failed", "file": file, "message": "%s could not be loaded" % file})
		elif ext in SCRIPT_EXTENSIONS:
			checked += 1
			var script := ResourceLoader.load(file, "", ResourceLoader.CACHE_MODE_IGNORE) as Script
			# An abstract class cannot be instantiated, but it is valid.
			if script == null or not (script.can_instantiate() or script.is_abstract()):
				issues.append({"kind": "script_error", "file": file, "message": "%s has errors" % file})
	var main_scene: String = ProjectSettings.get_setting("application/run/main_scene", "")
	if not main_scene.is_empty() and not _exists(_dependency_path(main_scene)):
		issues.append({"kind": "missing_main_scene", "file": "res://project.godot", "target": main_scene, "message": "the main scene %s does not exist" % main_scene})
	for prop in ProjectSettings.get_property_list():
		var name: String = prop.name
		if name.begins_with("autoload/"):
			var target := String(ProjectSettings.get_setting(name)).trim_prefix("*")
			if not _exists(_dependency_path(target)):
				issues.append({"kind": "missing_autoload", "file": "res://project.godot", "target": target, "message": "the autoload %s points to %s, which does not exist" % [name.trim_prefix("autoload/"), target]})
	return {"ok": issues.is_empty(), "command": "validate", "checked": checked, "issues": issues}


# The project's own input actions (not the built-in ui_* ones), with their events.
func _actions() -> Dictionary:
	var actions := []
	for prop in ProjectSettings.get_property_list():
		var name: String = prop.name
		if not name.begins_with("input/"):
			continue
		var action := name.trim_prefix("input/")
		var setting = ProjectSettings.get_setting(name)
		var events := []
		if typeof(setting) == TYPE_DICTIONARY:
			for e in setting.get("events", []):
				if e is InputEvent:
					events.append({"class": e.get_class(), "text": e.as_text()})
		actions.append({"name": action, "builtin": action.begins_with("ui_"), "deadzone": setting.get("deadzone", 0.5) if typeof(setting) == TYPE_DICTIONARY else 0.5, "events": events})
	actions = actions.filter(func(a): return not a.builtin)
	for a in actions:
		a.erase("builtin")
	return {"ok": true, "command": "actions", "actions": actions}


func _editor_properties(object: Object) -> Dictionary:
	var values := {}
	for p in object.get_property_list():
		if p.usage & PROPERTY_USAGE_EDITOR and not p.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP):
			values[p.name] = _value_out(object.get(p.name))
	return values


func _apply_values(object: Object, json_text: String) -> String:
	# Applies {"property": "value in text format"}; returns an error message or "".
	if json_text.is_empty():
		return ""
	var values = JSON.parse_string(json_text)
	if typeof(values) != TYPE_DICTIONARY:
		return "--set must be a JSON object"
	for key in values:
		if not key in object:
			return "no property '%s' in %s" % [key, object.get_class()]
		object.set_indexed(key, _value_in(str(values[key])))
	return ""


func _resource_command(command: String, args: PackedStringArray) -> Dictionary:
	var path := _option(args, "path")
	if not path.begins_with("res://") or not path.get_extension() in ["tres", "res"]:
		return _error("--path must be res://....tres or .res")
	var res: Resource
	if command == "res-new":
		var type := _option(args, "type")
		if not ClassDB.class_exists(type) or not ClassDB.is_parent_class(type, "Resource") or not ClassDB.can_instantiate(type):
			return _error("--type must be a resource class (e.g. StandardMaterial3D)")
		res = ClassDB.instantiate(type)
		var problem := _apply_values(res, _option(args, "set"))
		if not problem.is_empty():
			return _error(problem)
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
	else:
		res = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE) if ResourceLoader.exists(path) else null
		if res == null:
			return _error("could not load '%s'" % path)
	var prop := _option(args, "property")
	match command:
		"res-props":
			return {"ok": true, "command": command, "path": path, "class": res.get_class(), "properties": _editor_properties(res)}
		"res-get":
			if not prop.get_slice(":", 0) in res:
				return _error("no --property '%s' in %s" % [prop, res.get_class()])
			return {"ok": true, "command": command, "path": path, "property": prop, "value": _value_out(res.get_indexed(prop))}
		"res-set":
			if not prop.get_slice(":", 0) in res:
				return _error("no --property '%s' in %s" % [prop, res.get_class()])
			res.set_indexed(prop, _value_in(_option(args, "value")))
	var err := ResourceSaver.save(res, path)
	if err != OK:
		return _error("could not save '%s': %s" % [path, error_string(err)])
	var done := {"ok": true, "command": command, "path": path, "class": res.get_class()}
	if command == "res-set":
		done["property"] = prop
		done["value"] = _value_out(res.get_indexed(prop))
	return done


func _setting_command(command: String, args: PackedStringArray) -> Dictionary:
	var name := _option(args, "name")
	if name.is_empty() or not name.contains("/"):
		return _error("--name must be a setting path like application/config/name")
	if command == "setting-get":
		if not ProjectSettings.has_setting(name):
			return _error("no setting '%s'" % name)
		return {"ok": true, "command": command, "name": name, "value": _value_out(ProjectSettings.get_setting(name))}
	ProjectSettings.set_setting(name, _value_in(_option(args, "value")))
	var err := ProjectSettings.save()
	if err != OK:
		return _error("could not save project.godot: %s" % error_string(err))
	return {"ok": true, "command": command, "name": name, "value": _value_out(ProjectSettings.get_setting(name))}


const MOUSE_BUTTONS := {"left": MOUSE_BUTTON_LEFT, "right": MOUSE_BUTTON_RIGHT, "middle": MOUSE_BUTTON_MIDDLE, "wheel_up": MOUSE_BUTTON_WHEEL_UP, "wheel_down": MOUSE_BUTTON_WHEEL_DOWN}


func _action_command(command: String, args: PackedStringArray) -> Dictionary:
	var name := _option(args, "name")
	if name.is_empty() or name.contains("/"):
		return _error("--name must be an action name like jump")
	var key := "input/" + name
	if command == "action-remove":
		if not ProjectSettings.has_setting(key):
			return _error("no action '%s'" % name)
		ProjectSettings.set_setting(key, null)
	else:
		var current = ProjectSettings.get_setting(key) if ProjectSettings.has_setting(key) else {}
		var events: Array = [] if args.has("--replace") or typeof(current) != TYPE_DICTIONARY else current.get("events", []).duplicate()
		var key_name := _option(args, "key")
		if not key_name.is_empty():
			var code := OS.find_keycode_from_string(key_name)
			if code == KEY_NONE:
				return _error("unknown --key '%s' (use names like Space, A, Up, Escape)" % key_name)
			var e := InputEventKey.new()
			e.device = -1  # All devices, as the editor does.
			e.physical_keycode = code
			events.append(e)
		var mouse := _option(args, "mouse")
		if not mouse.is_empty():
			if not MOUSE_BUTTONS.has(mouse):
				return _error("--mouse must be one of %s" % ", ".join(MOUSE_BUTTONS.keys()))
			var e := InputEventMouseButton.new()
			e.device = -1
			e.button_index = MOUSE_BUTTONS[mouse]
			events.append(e)
		var joy := _option(args, "joy-button")
		if not joy.is_empty():
			var e := InputEventJoypadButton.new()
			e.device = -1
			e.button_index = joy.to_int()
			events.append(e)
		var deadzone := _option(args, "deadzone", str(current.get("deadzone", 0.2) if typeof(current) == TYPE_DICTIONARY else 0.2)).to_float()
		ProjectSettings.set_setting(key, {"deadzone": deadzone, "events": events})
	var err := ProjectSettings.save()
	if err != OK:
		return _error("could not save project.godot: %s" % error_string(err))
	var result := _actions()
	result["command"] = command
	return result


func _import_command(command: String, args: PackedStringArray) -> Dictionary:
	var path := _option(args, "path")
	var import_file := path + ".import"
	var cfg := ConfigFile.new()
	if cfg.load(import_file) != OK:
		return _error("no import settings for '%s' (run uzu.py engine <project> import first)" % path)
	if command == "import-get":
		var params := {}
		if cfg.has_section("params"):
			for k in cfg.get_section_keys("params"):
				params[k] = _value_out(cfg.get_value("params", k))
		return {"ok": true, "command": command, "path": path, "importer": cfg.get_value("remap", "importer", ""), "type": cfg.get_value("remap", "type", ""), "params": params}
	var param := _option(args, "param")
	if param.is_empty() or not cfg.has_section_key("params", param):
		return _error("--param must be one of the file's import params (see import-get)")
	cfg.set_value("params", param, _value_in(_option(args, "value")))
	var err := cfg.save(import_file)
	if err != OK:
		return _error("could not save '%s': %s" % [import_file, error_string(err)])
	return {"ok": true, "command": command, "path": path, "param": param, "value": _value_out(cfg.get_value("params", param)), "reimport": true}


const PLATFORMS := {"web": "Web", "linux": "Linux", "windows": "Windows Desktop", "macos": "macOS", "android": "Android"}
const PRESETS_FILE := "res://export_presets.cfg"


func _presets(cfg: ConfigFile) -> Array:
	var presets := []
	for section in cfg.get_sections():
		if section.begins_with("preset.") and not section.ends_with(".options"):
			var options := section + ".options"
			presets.append({"section": section, "name": cfg.get_value(section, "name", ""), "platform": cfg.get_value(section, "platform", ""), "export_path": cfg.get_value(section, "export_path", ""), "custom_template": cfg.get_value(options, "custom_template/release", "")})
	return presets


func _preset_command(command: String, args: PackedStringArray) -> Dictionary:
	var cfg := ConfigFile.new()
	if FileAccess.file_exists(PRESETS_FILE):
		cfg.load(PRESETS_FILE)
	if command == "presets":
		return {"ok": true, "command": command, "presets": _presets(cfg)}
	var platform := _option(args, "platform", "web")
	if not PLATFORMS.has(platform):
		return _error("--platform must be one of %s" % ", ".join(PLATFORMS.keys()))
	var name := _option(args, "name", PLATFORMS[platform])
	var section := ""
	var used := 0
	for preset in _presets(cfg):
		used += 1
		if preset.name == name:
			section = preset.section  # Same name: update that preset.
	if section.is_empty():
		section = "preset.%d" % used
	var out := _option(args, "out", "build/%s/%s" % [platform, "index.html" if platform == "web" else name.to_snake_case()])
	cfg.set_value(section, "name", name)
	cfg.set_value(section, "platform", PLATFORMS[platform])
	cfg.set_value(section, "runnable", true)
	cfg.set_value(section, "export_filter", "all_resources")
	cfg.set_value(section, "include_filter", "")  # The editor requires these two keys.
	cfg.set_value(section, "exclude_filter", "build/*")
	cfg.set_value(section, "export_path", out)
	var options := section + ".options"
	var template := _option(args, "template")
	if not template.is_empty():
		cfg.set_value(options, "custom_template/release", template)
		cfg.set_value(options, "custom_template/debug", template)
	if platform == "web":
		cfg.set_value(options, "variant/thread_support", false)  # Works without cross-origin isolation headers.
		cfg.set_value(options, "variant/extensions_support", false)
	elif not cfg.has_section(options):
		cfg.set_value(options, "custom_template/release", "")  # The editor fills the other options with defaults.
	var err := cfg.save(PRESETS_FILE)
	if err != OK:
		return _error("could not save export_presets.cfg: %s" % error_string(err))
	return {"ok": true, "command": command, "preset": name, "platform": PLATFORMS[platform], "export_path": out, "custom_template": template, "presets": _presets(cfg)}


const MAP_KINDS := {"tscn": "scene", "scn": "scene", "tres": "resource", "res": "resource", "gd": "script", "gdshader": "shader", "gdshaderinc": "shader"}


# A static index of the project for understanding it (B-204; validate, B-203, checks it).
# Nothing is instantiated, so no game code runs.
func _map() -> Dictionary:
	var files := []
	_project_files("res://", files)
	var kinds := {}
	var depends := {}
	var used_by := {}
	for file in files:
		if file.get_extension() == "import" or file.get_extension() == "uid":
			continue
		var kind: String = MAP_KINDS.get(file.get_extension().to_lower(), "other")
		if not kinds.has(kind):
			kinds[kind] = []
		kinds[kind].append(file)
		if kind == "other":
			continue
		var deps := []
		var entries := Array(ResourceLoader.get_dependencies(file))
		if kind == "script":
			entries.append_array(_script_references(file))  # preload/load/extends paths are not in the loader's list.
		for entry in entries:
			var dep := _dependency_path(entry)
			if not dep.is_empty() and not dep in deps:
				deps.append(dep)
				if not used_by.has(dep):
					used_by[dep] = []
				used_by[dep].append(file)
		deps.sort()
		depends[file] = deps
	for dep in used_by:
		used_by[dep].sort()

	var scenes := {}
	for file in kinds.get("scene", []):
		scenes[file] = _scene_map(file)

	var autoloads := []
	for prop in ProjectSettings.get_property_list():
		var name: String = prop.name
		if name.begins_with("autoload/"):
			var value := String(ProjectSettings.get_setting(name))
			autoloads.append({"name": name.trim_prefix("autoload/"), "path": value.trim_prefix("*"), "singleton": value.begins_with("*")})
	var classes := []
	var named := {}
	for c in ProjectSettings.get_global_class_list():
		classes.append({"class": c["class"], "base": c["base"], "path": c["path"]})
		named[c["path"]] = true
	# The class list is a cache the editor builds when it scans; read class_name from the scripts too.
	for file in kinds.get("script", []):
		if named.has(file):
			continue
		var source := FileAccess.get_file_as_string(file)
		var class_match := RegEx.create_from_string("(?m)^\\s*class_name\\s+(\\w+)").search(source)
		if class_match:
			var base_match := RegEx.create_from_string("(?m)^\\s*extends\\s+([\\w\\.\"/:]+)").search(source)
			classes.append({"class": class_match.get_string(1), "base": base_match.get_string(1).trim_prefix("\"").trim_suffix("\"") if base_match else "RefCounted", "path": file})
	classes.sort_custom(func(a, b): return a["class"] < b["class"])
	return {"ok": true, "command": "map", "main_scene": ProjectSettings.get_setting("application/run/main_scene", ""), "files": kinds, "depends_on": depends, "used_by": used_by, "scenes": scenes, "autoloads": autoloads, "classes": classes}


# Resource paths a GDScript names in preload(), load() and extends "..." (string literals).
func _script_references(path: String) -> Array:
	var found := []
	var source := FileAccess.get_file_as_string(path)
	var regex := RegEx.create_from_string("(?:preload|load)\\(\\s*\"(res://[^\"]+)\"|extends\\s+\"(res://[^\"]+)\"")
	for m in regex.search_all(source):
		var target := m.get_string(1) if not m.get_string(1).is_empty() else m.get_string(2)
		if not target in found:
			found.append(target)
	return found


# "./A/B" -> "A/B", "." stays the root.
func _clean_path(path: String) -> String:
	return path.trim_prefix("./") if path != "." else path


func _scene_map(path: String) -> Dictionary:
	var packed := ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	if packed == null:
		return {"error": "could not load"}
	var state := packed.get_state()
	var nodes := []
	var instances := []
	var scripts := []
	for i in state.get_node_count():
		var node_path := _clean_path(str(state.get_node_path(i)))
		var type := String(state.get_node_type(i))
		var instance := state.get_node_instance(i)
		if instance:
			instances.append({"node": node_path, "scene": instance.resource_path})
			if type.is_empty():
				type = "(instance)"
		for p in state.get_node_property_count(i):
			if state.get_node_property_name(i, p) == "script":
				var script = state.get_node_property_value(i, p)
				if script is Script:
					scripts.append({"node": node_path, "script": script.resource_path})
		nodes.append({"path": node_path, "type": type})
	var connections := []
	for c in state.get_connection_count():
		connections.append({"from": _clean_path(str(state.get_connection_source(c))), "signal": String(state.get_connection_signal(c)), "to": _clean_path(str(state.get_connection_target(c))), "method": String(state.get_connection_method(c))})
	return {"root": nodes[0]["type"] if not nodes.is_empty() else "", "node_count": nodes.size(), "nodes": nodes, "scripts": scripts, "instances": instances, "connections": connections}
