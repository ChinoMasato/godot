extends Node
## Input replay for the self-verification play (BACKLOG B-303). Installed as an
## autoload by ai_loop/selfplay.sh, so it runs before the scene's nodes each
## physics frame. Reads "--uzu-replay <file>" after "--": a JSON object
## {"events": [{"frame": 0, "action": "move_right", "pressed": true}, ...]},
## frames counted in physics frames from the start of the play.

var _events := []
var _next := 0
var _start := 0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_physics_priority = -1000
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-replay")
	if i < 0 or i + 1 >= args.size():
		return
	var data = JSON.parse_string(FileAccess.get_file_as_string(args[i + 1]))
	if typeof(data) != TYPE_DICTIONARY or typeof(data.get("events")) != TYPE_ARRAY:
		push_error("uzu replay: %s is not {\"events\": [...]}" % args[i + 1])
		return
	_events = data["events"]
	_events.sort_custom(func(a, b): return int(a["frame"]) < int(b["frame"]))
	_start = Engine.get_physics_frames()


func _physics_process(_delta: float) -> void:
	var frame := Engine.get_physics_frames() - _start
	while _next < _events.size() and int(_events[_next]["frame"]) <= frame:
		var e: Dictionary = _events[_next]
		if e.get("pressed", true):
			Input.action_press(e["action"])
		else:
			Input.action_release(e["action"])
		_next += 1
