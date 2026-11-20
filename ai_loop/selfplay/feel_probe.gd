extends Node
## Feel probe (BACKLOG B-1112): samples the *applied* state of UZUJuice
## once per process frame during a self-verifying play, so the feel of a
## game -- hitstop, slow motion, shake, kick, flashes, UI easing -- becomes
## numbers an agent can compare. Installed as an autoload by
## ai_loop/feel_report.py (through ai_loop/aiplayer.py); inert unless the
## user args carry "--uzu-feel <path>".
##
## Records (JSON Lines at the --uzu-feel path):
##   {"type": "sample", "pf", "frame", "msec", "ts", "juice_ts", "trauma",
##    "kick", "watch": {key: value}}   one per process frame in which any
##    sampled value changed (plus a heartbeat every 600 frames). pf is
##    Engine.get_process_frames(), frame is Engine.get_physics_frames(),
##    msec is the wall clock the juice deadlines themselves run on, ts is
##    Engine.time_scale and juice_ts is UZUJuice.applied_time_scale().
##   {"type": "ui_event", ..., "event", "phase", "preset", "target"}
##    new UZUUIJuice event-log entries, stamped with the frame counters.
##   {"type": "curve", "preset", "samples": [[t, value], ...]}
##    the easing curve of each UI preset first seen, sampled through
##    UZUUIJuice.motion_value at t = 0, 0.05, ..., 1.
##   {"type": "selftest", "action", "pf"}   with --uzu-feel-selftest the
##    probe itself fires hitstop / shake / slowmo / kick on a schedule, so
##    every channel can be exercised on any project (no gameplay needed).
##
## "--uzu-feel-watch Main/Player:modulate,..." adds NodePath:property
## values under /root to every sample (the convention of control.gd's
## state keys); the hikari driver watches the player's modulate (the
## pickup flash lives there) and the shard gauge's value (the count-up
## easing lives there).

const HEARTBEAT_FRAMES := 600
const EPS := 1e-9

var _file: FileAccess
var _watch_keys: Array = []
var _last_line := ""
var _last_heartbeat := 0
var _ui_seen := 0
var _curves_seen := {}
var _selftest := false
var _pf0 := -1
var _fired := {}


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_priority = 2000 # Sample after the juice runtime's own frame work.
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-feel")
	if i < 0 or i + 1 >= args.size():
		set_process(false)
		return
	var path := args[i + 1]
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	_file = FileAccess.open(path, FileAccess.WRITE)
	if _file == null:
		push_error("uzu feel probe: cannot open %s" % path)
		set_process(false)
		return
	var w := args.find("--uzu-feel-watch")
	if w >= 0 and w + 1 < args.size():
		for key in args[w + 1].split(","):
			var k := key.strip_edges()
			if not k.is_empty():
				_watch_keys.append(k)
	_selftest = args.has("--uzu-feel-selftest")
	_pf0 = Engine.get_process_frames()
	_write({"type": "start", "pf": _pf0, "frame": Engine.get_physics_frames(), "msec": Time.get_ticks_msec(), "watch": _watch_keys, "selftest": _selftest})


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


func _find_by_name(name: String) -> Node:
	var queue: Array = [get_tree().root]
	while not queue.is_empty():
		var n: Node = queue.pop_front()
		if n != get_tree().root and n.name == name:
			return n
		for c in n.get_children():
			queue.append(c)
	return null


func _read_watch(key: String) -> Variant:
	var colon := key.rfind(":")
	if colon < 0:
		return {"error": "expected NodePath:property"}
	var path := key.substr(0, colon)
	var node: Node
	if path.begins_with("find:"):
		# Node names are stabler than full paths through the anonymous
		# containers code-built UI is full of.
		node = _find_by_name(path.substr(5))
	else:
		node = get_tree().root.get_node_or_null(path)
	if node == null:
		return {"error": "no node %s" % path}
	return _json_value(node.get_indexed(key.substr(colon + 1)))


func _write(record: Dictionary) -> void:
	if _file:
		_file.store_line(JSON.stringify(record))
		_file.flush()


func _sample() -> Dictionary:
	var watch := {}
	for key in _watch_keys:
		watch[key] = _read_watch(key)
	return {
		"type": "sample",
		"pf": Engine.get_process_frames(),
		"frame": Engine.get_physics_frames(),
		"msec": Time.get_ticks_msec(),
		"ts": Engine.time_scale,
		"juice_ts": UZUJuice.applied_time_scale(),
		"trauma": UZUJuice.get_trauma(),
		"kick": UZUJuice.get_kick(),
		"watch": watch,
	}


func _changed(a: Dictionary, b: Dictionary) -> bool:
	for key in ["ts", "juice_ts", "trauma", "kick"]:
		if absf(float(a[key]) - float(b[key])) > EPS:
			return true
	return JSON.stringify(a["watch"]) != JSON.stringify(b["watch"])


func _poll_ui() -> void:
	var log = UZUUIJuice.get_event_log()
	while _ui_seen < log.size():
		var entry: Dictionary = log[_ui_seen]
		_ui_seen += 1
		var record := {"type": "ui_event", "pf": Engine.get_process_frames(), "frame": Engine.get_physics_frames(), "msec": Time.get_ticks_msec()}
		for k in entry:
			record[k] = _json_value(entry[k])
		_write(record)
		var preset := str(entry.get("preset", ""))
		if not preset.is_empty() and not _curves_seen.has(preset):
			_curves_seen[preset] = true
			var samples := []
			for s in range(21):
				var t := s / 20.0
				samples.append([t, UZUUIJuice.motion_value(preset, t)])
			_write({"type": "curve", "preset": preset, "samples": samples})


func _run_selftest(pf: int) -> void:
	var at := pf - _pf0
	var plan := {30: "hitstop", 150: "shake", 330: "slowmo", 540: "kick"}
	if not plan.has(at) or _fired.has(at):
		return
	_fired[at] = true
	match plan[at]:
		"hitstop":
			UZUJuice.hitstop(0.06)
		"shake":
			UZUJuice.shake(0.6)
		"slowmo":
			UZUJuice.slowmo(0.3, 0.3)
		"kick":
			UZUJuice.kick(0.8)
	_write({"type": "selftest", "action": plan[at], "pf": pf, "frame": Engine.get_physics_frames(), "msec": Time.get_ticks_msec()})


var _last_sample := {}


func _process(_delta: float) -> void:
	if _file == null:
		return
	if _selftest:
		_run_selftest(Engine.get_process_frames())
	_poll_ui()
	var sample := _sample()
	if _last_sample.is_empty() or _changed(sample, _last_sample) or sample["pf"] - _last_heartbeat >= HEARTBEAT_FRAMES:
		_write(sample)
		_last_sample = sample
		_last_heartbeat = int(sample["pf"])


func _exit_tree() -> void:
	if _file:
		_write(_sample())
		_write({"type": "end", "pf": Engine.get_process_frames(), "frame": Engine.get_physics_frames(), "msec": Time.get_ticks_msec()})
		_file = null
