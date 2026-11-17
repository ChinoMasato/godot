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
