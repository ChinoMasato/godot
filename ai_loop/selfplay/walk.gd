extends Node
## Walking self-check for generated maps (BACKLOG B-1507). Installed as an autoload by
## `ai_loop/uzu.py map <project> walk` in a copy of the project. It finds the map's places
## (the "uzu_places" metadata UZUWorldGen saves on the map's node), puts a walker (a
## CharacterBody3D with a capsule) at the start, and walks it to each target in turn with
## the path the navigation server gives, under physics: gravity, collisions, small steps
## up curbs. A map without a NavigationRegion3D (natural maps, so far) is an error.
## Writes, to the file given by "--uzu-walk <file> <from> <targets,...> <seconds>" after
## "--", one entry per target: reached or not, the time it took, the distance walked
## against the straight distance, and where it got stuck if it did. Then it quits.

const SPEED := 4.0
const STEP := 0.25  # Highest step the walker climbs (curbs are 0.15 m).

var _out := ""
var _from := "spawn"
var _targets: PackedStringArray = []
var _limit := 60.0
var _places := {}
var _body: CharacterBody3D
var _map := RID()
var _target := Vector3.ZERO
var _path := PackedVector3Array()
var _path_i := 0
var _wait_start := -1  # Wall-clock msec when the walker began waiting for a path.
var _index := -1
var _t := 0.0
var _walked := 0.0
var _best := INF
var _best_t := 0.0
var _last := Vector3.ZERO
var _results := []
const SETTLE_FRAMES := 120  # The most frames to wait for the navigation map.
var _settle := SETTLE_FRAMES


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-walk")
	if i < 0 or i + 4 >= args.size():
		set_physics_process(false)
		return
	_out = args[i + 1]
	_from = args[i + 2]
	_targets = args[i + 3].split(",", false)
	_limit = args[i + 4].to_float()
	set_physics_process(false)
	_start.call_deferred()


func _finish(error := "") -> void:
	var reached := _results.filter(func(r): return r.reached).size()
	var report := {"ok": error.is_empty() and reached == _results.size() and not _results.is_empty(), "from": _from, "walked": _results, "reached": reached, "targets": _results.size()}
	if not error.is_empty():
		report["error"] = error
	var f := FileAccess.open(_out, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(report))
	get_tree().quit()


func _pos(name: String):
	if not _places.has(name) or not _places[name] is Dictionary:
		return null
	var p: Array = _places[name].position
	return Vector3(p[0], p[1], p[2])


func _start() -> void:
	var holder: Node = null
	for n in [get_tree().current_scene] + get_tree().current_scene.find_children("*", "", true, false):
		if n.has_meta("uzu_places"):
			holder = n
			break
	if holder == null:
		_finish("no generated map in the scene (no node with uzu_places metadata)")
		return
	_places = holder.get_meta("uzu_places")
	if get_tree().current_scene.find_children("*", "NavigationRegion3D", true, false).is_empty():
		_finish("the map has no NavigationRegion3D to walk on (UZUWorldGen bakes one for cities: navigation=true)")
		return
	if _pos(_from) == null:
		_finish("no place '%s' (places: %s)" % [_from, ", ".join(_places.keys())])
		return
	if _targets.is_empty():
		# The places meant to be walked to: a city's plaza and gates; a natural map's villages.
		for k in ["plaza", "gate_north", "gate_east", "gate_south", "gate_west"]:
			if k != _from and _places.has(k):
				_targets.append(k)
		if _targets.is_empty() and _places.get("villages") is Array:
			for v in _places.villages:
				_places[v.name] = v
				if v.name != _from:
					_targets.append(v.name)
	_body = CharacterBody3D.new()
	_body.name = "UZUWalker"
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.35
	capsule.height = 1.7
	shape.shape = capsule
	shape.position = Vector3(0, 0.85, 0)
	_body.add_child(shape)
	get_tree().current_scene.add_child(_body)
	_map = _body.get_world_3d().navigation_map
	_body.global_position = _pos(_from) + Vector3(0, 0.3, 0)
	set_physics_process(true)


func _next_target() -> void:
	_index += 1
	if _index >= _targets.size():
		set_physics_process(false)
		_finish()
		return
	var target = _pos(_targets[_index])
	if target == null:
		_results.append({"to": _targets[_index], "reached": false, "error": "no such place"})
		_next_target()
		return
	_target = target
	_path = PackedVector3Array()
	_path_i = 0
	_t = 0.0
	_walked = 0.0
	_best = INF
	_best_t = 0.0
	_last = _body.global_position


func _flat(a: Vector3, b: Vector3) -> float:
	return Vector2(a.x - b.x, a.z - b.z).length()


func _physics_process(delta: float) -> void:
	if _settle > 0:  # Let the navigation map take in the region first (it syncs asynchronously).
		_settle -= 1
		var synced := NavigationServer3D.map_get_iteration_id(_map) > 0
		if _settle == 0 or (synced and _settle < SETTLE_FRAMES - 3):
			_settle = 0
			_next_target()
		return
	# The path straight from the navigation server; followed by the flat (xz) distance, because
	# the baked mesh sits up to a cell above the floor.
	if _path.is_empty():
		_path = NavigationServer3D.map_get_path(_map, _body.global_position, _target, true)
		_path_i = 0
		if _path.is_empty():
			# The map takes in a large mesh on a worker thread, in wall-clock time, while
			# --fixed-fps runs the game faster than real time: wait on the wall clock.
			if _wait_start < 0:
				_wait_start = Time.get_ticks_msec()
			if Time.get_ticks_msec() - _wait_start < 60000:
				return
	while _path_i < _path.size() - 1 and _flat(_body.global_position, _path[_path_i]) < 0.15:  # Turning sooner cuts corners into walls.
		_path_i += 1
	var target: Vector3 = _target
	var here := _body.global_position
	_t += delta
	_walked += _flat(here, _last)
	_last = here
	var left := _flat(here, target)
	if left < _best - 0.5:
		_best = left
		_best_t = _t
	var done := left < 1.5
	var stuck := _t - _best_t > 4.0
	if done or stuck or _t > _limit:
		var start = _pos(_from) if _index == 0 else _pos(_targets[_index - 1])
		var entry := {"to": _targets[_index], "reached": done, "seconds": snappedf(_t, 0.01), "walked_m": snappedf(_walked, 0.1), "straight_m": snappedf(_flat(start, target), 0.1)}
		if not done:
			entry["stuck_at"] = [snappedf(here.x, 0.1), snappedf(here.y, 0.1), snappedf(here.z, 0.1)]
			entry["left_m"] = snappedf(left, 0.1)
			var np: Vector3 = _path[_path_i] if _path_i < _path.size() else here
			entry["next_point"] = [snappedf(np.x, 0.1), snappedf(np.y, 0.1), snappedf(np.z, 0.1)]
			entry["path_points"] = _path.size()
			entry["on_wall"] = _body.is_on_wall()
			entry["on_floor"] = _body.is_on_floor()
			var hits := []
			for c in _body.get_slide_collision_count():
				var col := _body.get_slide_collision(c)
				hits.append({"with": str(col.get_collider().name) if col.get_collider() else "", "shape": str(col.get_collider_shape().name) if col.get_collider_shape() else "", "normal": [snappedf(col.get_normal().x, 0.01), snappedf(col.get_normal().y, 0.01), snappedf(col.get_normal().z, 0.01)]})
			entry["hits"] = hits
		_results.append(entry)
		_next_target()
		return
	var next: Vector3 = _path[_path_i] if _path_i < _path.size() else here
	var dir := next - here
	dir.y = 0
	if dir.length() > 0.01:
		dir = dir.normalized() * SPEED
	_body.velocity.x = dir.x
	_body.velocity.z = dir.z
	_body.velocity.y = 0.0 if _body.is_on_floor() else _body.velocity.y - 20.0 * delta
	var before := _body.global_position
	_body.move_and_slide()
	# Step up curbs: when blocked while walking, rise one step, but only if the way on is
	# free up there (a wall stays a wall).
	if _body.is_on_wall() and _flat(_body.global_position, before) < SPEED * delta * 0.3 and dir.length() > 0.01:
		var raised := _body.global_transform.translated(Vector3(0, STEP, 0))
		if not _body.test_move(raised, Vector3(dir.x, 0, dir.z).normalized() * 0.3):
			_body.global_position += Vector3(0, STEP, 0)
