extends Node
## Game state hashing for determinism checks (BACKLOG B-307). Installed as an autoload
## by ai_loop/uzu.py selfplay --hash-every N. After the game's own physics step, every
## N frames, it hashes the declared state and writes {"every", "hashes": {frame: sha256},
## "state": {...last state...}} to "--uzu-state-hash <file>" when the game ends.
##
## A game declares its state by putting nodes in the group "uzu_state" (the player in
## "uzu_player" counts too). For each node the hash covers its transform and either the
## Dictionary its uzu_state() method returns, or the properties named in its metadata
## "uzu_state" (an Array of names). Values are written in Godot's text format in node
## path order, so the same state always gives the same hash.

var _every := 0
var _path := ""
var _start := 0
var _hashes := {}
var _last_state := {}


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_physics_priority = 1 << 30  # After every other node's physics step.
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-state-hash")
	if i >= 0 and i + 1 < args.size():
		_path = args[i + 1]
	var e := args.find("--uzu-hash-every")
	_every = args[e + 1].to_int() if e >= 0 and e + 1 < args.size() else 30
	_start = Engine.get_physics_frames()


func state() -> Dictionary:
	var nodes := {}
	for group in ["uzu_state", "uzu_player"]:
		for node in get_tree().get_nodes_in_group(group):
			nodes[str(node.get_path())] = node
	var paths := nodes.keys()
	paths.sort()
	var out := {}
	for path in paths:
		var node: Node = nodes[path]
		var entry := {}
		if node is Node3D:
			entry["transform"] = var_to_str((node as Node3D).global_transform)
		elif node is Node2D:
			entry["transform"] = var_to_str((node as Node2D).global_transform)
		if node.has_method("uzu_state"):
			var declared: Dictionary = node.call("uzu_state")
			for key in declared:
				entry[str(key)] = var_to_str(declared[key])
		elif node.has_meta("uzu_state"):
			for property in node.get_meta("uzu_state"):
				entry[str(property)] = var_to_str(node.get(property))
		out[path] = entry
	return out


func state_hash() -> String:
	var s := state()
	_last_state = s
	return JSON.stringify(s, "", true).sha256_text()


func _physics_process(_delta: float) -> void:
	var frame := Engine.get_physics_frames() - _start
	if _every > 0 and frame > 0 and frame % _every == 0:
		_hashes[str(frame)] = state_hash()


func _exit_tree() -> void:
	if _path.is_empty():
		return
	var f := FileAccess.open(_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"every": _every, "hashes": _hashes, "state": _last_state}))
