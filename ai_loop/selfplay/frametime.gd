extends Node
## Frame time recorder (BACKLOG B-803). Installed as an autoload by ai_loop/bench.py
## in a copy of the project. After a warm-up it records, for every frame:
##   work: the time from the start of the frame's physics/process step to the end
##         of the last _process (a marker node that runs after every other node).
##         Headless runs pad each frame with a sleep, so this, not the wall-clock
##         time, shows how heavy the game's frames are. (The Performance monitors
##         only update once a second, so they cannot give per-frame times.)
##   wall: the wall-clock time between frames (meaningful when frames are drawn).
## Writes {"frames", "work": {"average_ms", "p95_ms", "worst_ms"}, "wall": {...}} to the
## file given by "--uzu-frametime <file>" after "--" when the game ends.

const WARMUP_FRAMES := 10

var _path := ""
var _last := 0
var _seen := 0
var _frame_start := 0
var _wall := PackedFloat64Array()
var _work := PackedFloat64Array()


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_priority = -3000
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-frametime")
	if i >= 0 and i + 1 < args.size():
		_path = args[i + 1]
	get_tree().physics_frame.connect(_begin)
	get_tree().process_frame.connect(_begin)
	var marker := EndMarker.new()
	marker.recorder = self
	marker.process_priority = 1 << 30  # After every other node's _process.
	add_child(marker)


class EndMarker:
	extends Node
	var recorder: Node

	func _process(_delta: float) -> void:
		recorder._end()


func _begin() -> void:
	if _frame_start == 0:
		_frame_start = Time.get_ticks_usec()


func _end() -> void:
	if _frame_start != 0 and _seen > WARMUP_FRAMES:
		_work.append((Time.get_ticks_usec() - _frame_start) / 1000.0)
	_frame_start = 0


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	_seen += 1
	if _seen > WARMUP_FRAMES and _last > 0:
		_wall.append((now - _last) / 1000.0)
	_last = now


func _stats(times: PackedFloat64Array) -> Dictionary:
	var sorted := times.duplicate()
	sorted.sort()
	var total := 0.0
	for t in sorted:
		total += t
	return {"average_ms": total / sorted.size(), "p95_ms": sorted[int(floor((sorted.size() - 1) * 0.95))], "worst_ms": sorted[sorted.size() - 1]}


func _exit_tree() -> void:
	if _path.is_empty() or _wall.is_empty() or _work.is_empty():
		return
	var result := {"frames": _wall.size(), "work": _stats(_work), "wall": _stats(_wall)}
	var f := FileAccess.open(_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(result))
