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
##   pbr-make     --in res://tex.png --out res://materials/stone [--kind stone] [--strength 1.0] [--delight 0.7]
##                [--tile] [--invert] [--size 1024] [--normal-from res://n.png]   estimated PBR maps + material.tres (B-902)
##   pbr-compare  --normal res://n.png --ref res://scan_n.png [--orm res://orm.png --ref-orm res://scan_arm.png]
##   pbr-kinds                                                    the kinds of material and their values
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
		"pbr-make":
			var options := {"strength": _option(args, "strength", "1.0").to_float(), "delight": _option(args, "delight", "0.7").to_float(), "size": _option(args, "size", "1024").to_int(), "tile": args.has("--tile"), "invert": args.has("--invert")}
			if not _option(args, "normal-from").is_empty():
				options["normal_from"] = _option(args, "normal-from")
			var kind := _option(args, "kind", "stone")
			if not UZUPBRFromImage.get_kinds().has(kind):
				return _error("unknown --kind '%s' (see pbr-kinds)" % kind)
			var made := UZUPBRFromImage.make_file(_option(args, "in"), _option(args, "out"), kind, options)
			made["command"] = command
			return made
		"pbr-compare":
			var images := {}
			for key in ["normal", "ref", "orm", "ref-orm"]:
				var path := _option(args, key)
				images[key] = Image.load_from_file(ProjectSettings.globalize_path(path)) if not path.is_empty() else null
			if images["normal"] == null or images["ref"] == null:
				return _error("pbr-compare needs readable --normal and --ref (and optionally --orm and --ref-orm)")
			var compared := UZUPBRFromImage.compare(images["normal"], images["ref"], images["orm"], images["ref-orm"])
			compared["command"] = command
			return compared
		"pbr-kinds":
			return {"ok": true, "command": command, "kinds": UZUPBRFromImage.get_kinds()}
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
