extends Node
## Lockstep control for an outside AI player (BACKLOG B-306). Installed as an
## autoload by ai_loop/aiplayer.py. At the start of every step it prints one
## line "UZU_STEP {json}" (the observation) to stdout and waits for one line of
## JSON on stdin (the next command), so the game only moves when told to.
##
## Command: {"frames": 1, "press": ["jump"], "release": [], "state": ["Main/Player:position"],
##           "image": "/tmp/frame.png", "reset": false, "quit": false}
##   frames   physics frames to advance before the next observation (default 1)
##   press / release   input actions to press or release now
##   state    "NodePath:property" under /root, read in the next observation
##   image    save the next observation's frame there (needs a renderer, not --headless)
##   reset    reload the current scene and release all actions
##   save     "name": snapshot the declared state into a slot (B-309; needs UZUStateHash)
##   restore  "name": put a saved state back, with the actions pressed at the time
##   hash     true: add "state_hash" (B-307) to the next observation
## "--uzu-seed <n>" after "--" fixes the global random seed (B-305).
## Observation: {"frame", "events": [telemetry events since the last one],
##   "state": {"Main/Player:position": [x, y]}, "image": path or null, "done": cleared,
##   "reset": true right after a reset, "ended": true when the game quit by itself (the last one)}

const PREFIX := "UZU_STEP "

var _remaining := 0
var _events := []
var _state_keys := []
var _image := ""
var _pressed := {}
var _done := false
var _reset := false
var _ended := false
var _slots := {}
var _notes := {}
var _want_hash := false


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_physics_priority = -2000
	var args := OS.get_cmdline_user_args()
	var s := args.find("--uzu-seed")
	if s >= 0 and s + 1 < args.size():
		seed(args[s + 1].to_int())  # As replay.gd (B-305), before the game's nodes start.
	var telemetry := get_node_or_null("/root/UZUTelemetry")
	if telemetry:
		telemetry.event_recorded.connect(_on_event)


# B-310: a press must look like a player's press. Input.action_press alone
# only sets the action state, so games that read input through
# _unhandled_input (event.is_action_pressed) never see it. Dispatch the
# matching InputEventAction as well; games that poll (is_action_just_pressed
# / get_vector) keep reading the state exactly as before.
func _send_action_event(action: StringName, pressed: bool) -> void:
	var ev := InputEventAction.new()
	ev.action = action
	ev.pressed = pressed
	ev.strength = 1.0 if pressed else 0.0
	Input.parse_input_event(ev)


func _on_event(event: Dictionary) -> void:
	_events.append(event)
	if event.get("type") == "clear":
		_done = true


func _physics_process(_delta: float) -> void:
	if _remaining > 0:
		_remaining -= 1
		if _remaining > 0:
			return
	_observe()
	_command()
	_remaining = max(_remaining, 0)


func _json_value(value: Variant) -> Variant:
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return value
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_STRING_NAME, TYPE_NODE_PATH:
			return str(value)
		TYPE_ARRAY:
			return (value as Array).map(_json_value)
		TYPE_DICTIONARY:
			var d := {}
			for k in value:
				d[str(k)] = _json_value(value[k])
			return d
	return var_to_str(value)


func _read_state(key: String) -> Variant:
	var colon := key.rfind(":")
	if colon < 0:
		return {"error": "expected NodePath:property"}
	var node := get_tree().root.get_node_or_null(key.substr(0, colon))
	if node == null:
		return {"error": "no node %s" % key.substr(0, colon)}
	return _json_value(node.get_indexed(key.substr(colon + 1)))


func _observe() -> void:
	var state := {}
	for key in _state_keys:
		state[key] = _read_state(key)
	var image_path = null
	if not _image.is_empty():
		var image := get_viewport().get_texture().get_image()
		if image and not image.is_empty():
			image.save_png(_image)
			image_path = _image
	var observation := {"frame": Engine.get_physics_frames(), "events": _events, "state": state, "image": image_path, "done": _done, "reset": _reset, "ended": _ended}
	var hasher := get_node_or_null("/root/UZUStateHash")
	if _want_hash and hasher:
		observation["state_hash"] = hasher.state_hash()
	observation.merge(_notes)
	_notes = {}
	_want_hash = false
	print(PREFIX + JSON.stringify(observation))
	_events = []
	_image = ""
	_reset = false


func _command() -> void:
	var line := OS.read_string_from_stdin(1 << 20)
	if line.strip_edges().is_empty():
		_ended = true  # The controller went away.
		get_tree().quit()
		return
	var cmd = JSON.parse_string(line)
	if typeof(cmd) != TYPE_DICTIONARY:
		push_error("uzu control: not a JSON object: %s" % line)
		_remaining = 1
		return
	if cmd.get("quit", false):
		_ended = true  # No last observation: the controller asked for the end.
		get_tree().quit()
		return
	_state_keys = cmd.get("state", _state_keys)
	_image = cmd.get("image", "")
	_want_hash = cmd.get("hash", false)
	var hasher := get_node_or_null("/root/UZUStateHash")
	if cmd.has("save") or cmd.has("restore"):
		if hasher == null:
			_notes["error"] = "save states need the UZUStateHash autoload"
		elif cmd.has("save"):
			_slots[str(cmd["save"])] = {"snapshot": hasher.snapshot(), "pressed": _pressed.duplicate(), "done": _done}
			_notes["saved"] = str(cmd["save"])
		elif _slots.has(str(cmd["restore"])):
			var slot: Dictionary = _slots[str(cmd["restore"])]
			for action in _pressed:
				Input.action_release(action)
			_pressed = slot["pressed"].duplicate()
			for action in _pressed:
				Input.action_press(action)
			_done = slot["done"]
			_notes["restored"] = str(cmd["restore"])
			_notes["restored_nodes"] = hasher.restore(slot["snapshot"])
		else:
			_notes["error"] = "no saved state named '%s'" % cmd["restore"]
	if cmd.get("reset", false):
		for action in _pressed:
			Input.action_release(action)
		_pressed.clear()
		_done = false
		_reset = true
		get_tree().reload_current_scene()
		_remaining = 1
		return
	for action in cmd.get("release", []):
		Input.action_release(action)
		_send_action_event(action, false)
		_pressed.erase(action)
	for action in cmd.get("press", []):
		Input.action_press(action)
		_send_action_event(action, true)
		_pressed[action] = true
	_remaining = max(int(cmd.get("frames", 1)), 1)


# The game quit by itself (for example on the clear): send the last observation,
# with "ended": true, so the controller sees the final events.
func _exit_tree() -> void:
	if not _ended:
		_ended = true
		_state_keys = []
		_observe()
