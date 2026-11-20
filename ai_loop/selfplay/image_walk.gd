extends Node
## Walking self-check for image-first scaffolds (BACKLOG B-1221). Installed as
## an autoload by `ai_loop/uzu.py image-scene --walk` in a copy of the project.
## There is no navigation mesh in a scaffold, so the walker (a CharacterBody3D
## with a capsule) steers straight at each waypoint under physics: gravity,
## collisions with the ground body and the card bodies, floor snap. The
## targets come from a JSON file the generator wrote:
##   {"spawn": [x, y, z], "waypoints": [[x, y, z], ...]}
## Writes, to the file given by "--uzu-image-walk <file> <targets.json>
## <seconds>" after "--", one entry per waypoint (reached, seconds, frames,
## where it got stuck) plus whether the walker ever fell through the floor.
## Then it quits.

const SPEED := 3.5
const REACH := 0.7  # Horizontal distance that counts as arrived.
const STUCK_AFTER := 3.0  # Seconds without getting closer before giving up.

var _out := ""
var _targets_path := ""
var _limit := 60.0
var _body: CharacterBody3D
var _waypoints: Array = []
var _index := -1
var _target := Vector3.ZERO
var _t := 0.0
var _frames := 0
var _best := INF
var _best_t := 0.0
var _fell := false
var _results := []


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-image-walk")
	if i < 0 or i + 3 >= args.size():
		set_physics_process(false)
		return
	_out = args[i + 1]
	_targets_path = args[i + 2]
	_limit = args[i + 3].to_float()
	set_physics_process(false)
	_start.call_deferred()


func _finish(error := "") -> void:
	var reached := _results.filter(func(r): return r.reached).size()
	var report := {"ok": error.is_empty() and not _fell and reached == _results.size() and not _results.is_empty(),
		"walked": _results, "reached": reached, "targets": _results.size(), "fell_through": _fell}
	if not error.is_empty():
		report["error"] = error
	var f := FileAccess.open(_out, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(report))
	get_tree().quit()


func _start() -> void:
	var text := FileAccess.get_file_as_string(_targets_path)
	var spec = JSON.parse_string(text)
	if not spec is Dictionary or not spec.has("waypoints"):
		_finish("no walk targets at %s" % _targets_path)
		return
	var spawn: Array = spec.get("spawn", [0, 0, 0])
	_waypoints = spec.waypoints
	_body = CharacterBody3D.new()
	_body.name = "ImageWalker"
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.35
	capsule.height = 1.7
	shape.shape = capsule
	_body.add_child(shape)
	_body.position = Vector3(spawn[0], float(spawn[1]) + 1.0, spawn[2])
	_body.floor_snap_length = 0.3
	get_tree().current_scene.add_child(_body)
	_next()
	set_physics_process(true)


func _next() -> void:
	_index += 1
	if _index >= _waypoints.size():
		_finish()
		return
	var t: Array = _waypoints[_index]
	_target = Vector3(t[0], t[1], t[2])
	_t = 0.0
	_frames = 0
	_best = INF
	_best_t = 0.0


func _physics_process(delta: float) -> void:
	if _body == null:
		return
	_t += delta
	_frames += 1
	if _body.global_position.y < -2.0:
		_fell = true
		_record(false)
		_next()
		return
	var to := _target - _body.global_position
	to.y = 0.0
	var dist := to.length()
	if dist < _best - 0.05:
		_best = dist
		_best_t = _t
	if dist <= REACH:
		_record(true)
		_next()
		return
	if _t - _best_t > STUCK_AFTER or _t > _limit:
		_record(false)
		_next()
		return
	var dir := to / dist
	_body.velocity.x = dir.x * SPEED
	_body.velocity.z = dir.z * SPEED
	_body.velocity.y -= 9.8 * delta
	_body.move_and_slide()


func _record(reached: bool) -> void:
	var entry := {"target": [_target.x, _target.y, _target.z], "reached": reached,
		"seconds": snappedf(_t, 0.01), "frames": _frames}
	if not reached:
		var p := _body.global_position
		entry["stuck_at"] = [snappedf(p.x, 0.01), snappedf(p.y, 0.01), snappedf(p.z, 0.01)]
	_results.append(entry)
