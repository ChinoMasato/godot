extends Node
## Region-graph walking verification (BACKLOG B-1510). Installed as an autoload by
## `ai_loop/uzu.py map <project> walk-graph` in a copy of the project. It reads a region
## graph (B-1509, format uzu-region-graph/1: nodes with world centers, bidirectional
## edges with lengths) and walks EVERY edge in BOTH directions with a walker body
## (a CharacterBody3D with a capsule) under physics: gravity, collisions, small steps
## up curbs. Where the scene has a navigation mesh and the walker is on it, the
## navigation server's path guides the walker; off the mesh (natural terrain has
## none) the walker steers straight at the target, and when that is blocked it
## first walks to the closest point of the mesh — the walkable network — and
## lets the path take it around. Either way the evidence is the body actually
## arriving — never the static existence of a path.
## Per leg it records: reached or not, physics frames and seconds taken, meters
## walked, and where it got stuck. Per edge: ok only when both legs arrived.
## Writes the report JSON to the file named by the user arguments:
##   --uzu-walk-graph <graph.json> <out.json> <cap-seconds|0> <block-node|->
## A block node is fenced in by four walls before the walk starts, to prove that
## exactly its edges fail (the counter-check of the BACKLOG judgement).
##
## Preconditions: the scene named by the run must already contain the generated
## map the graph describes (for the hikari sample, compose_village.gd saves
## res://village.tscn first). The navigation mesh syncs asynchronously, so the
## walker waits for the map's first iteration before the first leg, and waits
## for the body to land after every teleport.

const SPEED := 4.0
const STEP := 0.25  # Highest step the walker climbs (curbs are 0.15 m).
const ARRIVE := 1.5  # Flat distance that counts as arrived (as in walk.gd).
const STUCK_AFTER := 5.0  # Seconds without progress before a leg is stuck.
const REPLAN := 20  # Physics frames between navigation re-plans.

var _out := ""
var _cap := 0.0
var _block := ""
var _nodes := {}  # id -> Vector3 (world center from the graph)
var _edges := []  # [{a, b, length}]
var _scene_name := ""

var _body: CharacterBody3D
var _map := RID()
var _has_nav := false

var _state := "settle"  # settle -> drop -> walk -> (next leg) -> done
var _settle := 120
var _leg := -1  # Index into _legs.
var _legs := []  # [{from_id, to_id, from_pos, to_pos, edge_index}]
var _results := []  # Per edge: {a, b, length, legs: [...]}
var _target := Vector3.ZERO
var _frames := 0
var _t := 0.0
var _walked := 0.0
var _best := INF
var _best_t := 0.0
var _last := Vector3.ZERO
var _path := PackedVector3Array()
var _path_i := 0
var _mode := "direct"  # direct | nav | seek (to the closest mesh point) | wall (along a wall)
var _nav_done := false  # The nav path ran out on this leg; stay direct.
var _used_nav := false
var _replan := 0
var _drop := 0
var _anchor := Vector3.ZERO  # Last position the walker measurably moved from.
var _anchor_t := 0.0
var _seek_point := Vector3.ZERO
var _seeks := 0
var _last_seek := Vector3(1.0e9, 0, 1.0e9)  # Previous seek target; a repeat is a cycle.
const MAX_SEEKS := 6
var _wall_dir := Vector3.ZERO
var _wall_t := 0.0
var _wall_clear := 0.0  # Time in wall mode spent off the wall (got up / around).
var _wall_entries := 0
var _wall_flipped := false
var _wall_away := false  # Alternate the initial wall direction per entry.
const MAX_WALLS := 10
const WALL_LIMIT := 45.0  # Seconds per wall-follow entry.


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-walk-graph")
	if i < 0 or i + 4 >= args.size():
		set_physics_process(false)
		return
	_out = args[i + 2]
	_cap = args[i + 3].to_float()
	_block = args[i + 4]
	set_physics_process(false)
	_start.call_deferred(args[i + 1])


func _finish(error := "") -> void:
	var edges_ok := 0
	for r in _results:
		if r.get("ok", false):
			edges_ok += 1
	var report := {
		"format": "uzu-walk-graph-report/1",
		"ok": error.is_empty() and edges_ok == _results.size() and not _results.is_empty(),
		"scene": _scene_name,
		"blocked_node": _block if not _block.is_empty() and _block != "-" else null,
		"nodes": _nodes.size(),
		"edges": _results.size(),
		"legs": _legs.size(),
		"edges_ok": edges_ok,
		"edges_failed": _results.size() - edges_ok,
		"results": _results,
	}
	if not error.is_empty():
		report["error"] = error
	var f := FileAccess.open(_out, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(report, "\t"))
	get_tree().quit()


func _flat(a: Vector3, b: Vector3) -> float:
	return Vector2(a.x - b.x, a.z - b.z).length()


func _start(graph_path: String) -> void:
	var text := FileAccess.get_file_as_string(graph_path)
	var graph = JSON.parse_string(text) if not text.is_empty() else null
	if not graph is Dictionary or graph.get("format", "") != "uzu-region-graph/1":
		_finish("not a region graph (format uzu-region-graph/1): " + graph_path)
		return
	_scene_name = graph.get("scene", "")
	for n in graph.get("nodes", []):
		var c: Array = n["center"]
		_nodes[n["id"]] = Vector3(c[0], c[1], c[2])
	for e in graph.get("edges", []):
		_edges.append({"a": e["a"], "b": e["b"], "length": float(e.get("length", 0.0))})
		_results.append({"a": e["a"], "b": e["b"], "length": float(e.get("length", 0.0)), "ok": false, "legs": []})
		_legs.append({"edge": _edges.size() - 1, "from": e["a"], "to": e["b"]})
		_legs.append({"edge": _edges.size() - 1, "from": e["b"], "to": e["a"]})
	if _nodes.is_empty() or _edges.is_empty():
		_finish("the region graph has no nodes or no edges")
		return
	_has_nav = not get_tree().current_scene.find_children("*", "NavigationRegion3D", true, false).is_empty()
	if _block != "" and _block != "-":
		if not _nodes.has(_block):
			_finish("no node '%s' to block (nodes: %s)" % [_block, ", ".join(_nodes.keys())])
			return
		_fence(_nodes[_block])
	_body = CharacterBody3D.new()
	_body.name = "UZUGraphWalker"
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.35
	capsule.height = 1.7
	shape.shape = capsule
	shape.position = Vector3(0, 0.85, 0)
	_body.add_child(shape)
	get_tree().current_scene.add_child(_body)
	_map = _body.get_world_3d().navigation_map
	_body.global_position = _nodes[_legs[0]["from"]] + Vector3(0, 0.4, 0)
	set_physics_process(true)


## Fences a node in: four walls (3 m high) forming a square around its center,
## so legs from it cannot leave and legs to it cannot enter.
func _fence(center: Vector3) -> void:
	var body := StaticBody3D.new()
	body.name = "UZUBlockFence"
	var half := 2.4
	var thick := 0.5
	var height := 3.0
	var y := center.y + height * 0.5 - 0.1
	var walls := [
		[Vector3(center.x - half, y, center.z), Vector3(thick, height, 2 * half + thick)],
		[Vector3(center.x + half, y, center.z), Vector3(thick, height, 2 * half + thick)],
		[Vector3(center.x, y, center.z - half), Vector3(2 * half + thick, height, thick)],
		[Vector3(center.x, y, center.z + half), Vector3(2 * half + thick, height, thick)],
	]
	for w in walls:
		var cs := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = w[1]
		cs.shape = box
		cs.position = w[0]
		body.add_child(cs)
	get_tree().current_scene.add_child(body)


func _leg_cap() -> float:
	if _cap > 0.0:
		return _cap
	var length := float(_edges[_legs[_leg]["edge"]]["length"])
	return clampf(30.0 + length * 1.25, 45.0, 600.0)


func _next_leg() -> void:
	_leg += 1
	if _leg >= _legs.size():
		_state = "done"
		set_physics_process(false)
		_finish()
		return
	var leg: Dictionary = _legs[_leg]
	_target = _nodes[leg["to"]]
	_body.global_position = _nodes[leg["from"]] + Vector3(0, 0.4, 0)
	_body.velocity = Vector3.ZERO
	_frames = 0
	_t = 0.0
	_walked = 0.0
	_best = INF
	_best_t = 0.0
	_last = _body.global_position
	_path = PackedVector3Array()
	_path_i = 0
	_mode = "direct"
	_nav_done = false
	_used_nav = false
	_replan = 0
	_drop = 0
	_anchor = _body.global_position
	_anchor_t = 0.0
	_seeks = 0
	_last_seek = Vector3(1.0e9, 0, 1.0e9)
	_wall_t = 0.0
	_wall_clear = 0.0
	_wall_entries = 0
	_wall_flipped = false
	_wall_away = false
	_state = "drop"


func _end_leg(reached: bool, reason := "") -> void:
	_state = "between"  # The teleport of the next leg happens deferred, outside the physics step.
	var leg: Dictionary = _legs[_leg]
	var here := _body.global_position
	var entry := {
		"from": leg["from"], "to": leg["to"], "reached": reached,
		"frames": _frames, "seconds": snappedf(_t, 0.01),
		"walked_m": snappedf(_walked, 0.1), "used_nav": _used_nav, "seeks": _seeks, "walls": _wall_entries,
		"stuck_at": null, "left_m": snappedf(_flat(here, _target), 0.1),
	}
	if not reached:
		entry["stuck_at"] = [snappedf(here.x, 0.1), snappedf(here.y, 0.1), snappedf(here.z, 0.1)]
		entry["reason"] = reason
		entry["on_wall"] = _body.is_on_wall()
		entry["on_floor"] = _body.is_on_floor()
		var hits := []
		for c in _body.get_slide_collision_count():
			var col := _body.get_slide_collision(c)
			hits.append({"with": str(col.get_collider().name) if col.get_collider() else "", "normal": [snappedf(col.get_normal().x, 0.01), snappedf(col.get_normal().y, 0.01), snappedf(col.get_normal().z, 0.01)]})
		entry["hits"] = hits
	var edge_result: Dictionary = _results[leg["edge"]]
	edge_result["legs"].append(entry)
	if edge_result["legs"].size() == 2:
		edge_result["ok"] = bool(edge_result["legs"][0]["reached"]) and bool(edge_result["legs"][1]["reached"])
	_next_leg.call_deferred()


## The fallback chain when a tripwire fires. Returns false when the leg
## was ended as stuck. Nav gives up its guidance to direct steering (a
## detour the distance metric cannot see, or a wall the mesh does not
## know). Direct steering, blocked by something the walkable network
## knows how to go around (a building, the city foundation's edge),
## walks to the closest point of the navigation mesh when that point is
## somewhere to go; when the mesh is right there but vertically out of
## step reach (the walker stands at the foot of the foundation slab,
## the mesh on top of it), it follows the wall instead, and the step-up
## that runs every frame takes it up wherever the ground outside rises
## close enough to the top. Only a leg that stalls through the whole
## chain is stuck.
func _blocked(here: Vector3, left: float) -> bool:
	if _mode == "nav":
		_mode = "direct"
		_nav_done = true
	elif _mode == "direct" or _mode == "seek":
		if _mode == "direct" and _has_nav and _seeks < MAX_SEEKS:
			var closest := NavigationServer3D.map_get_closest_point(_map, here)
			if _flat(here, closest) > 1.0 and _flat(here, closest) <= 40.0 and _flat(closest, _last_seek) > 3.0:
				_mode = "seek"
				_seek_point = closest
				_last_seek = closest
				_seeks += 1
				_best = left
				_best_t = _t
				_anchor = here
				_anchor_t = _t
				return true
		if _wall_entries < MAX_WALLS:
			_enter_wall(here)
			_best = left
			_best_t = _t
			_anchor = here
			_anchor_t = _t
			return true
		_end_leg(false, "stuck")
		return false
	else:
		_end_leg(false, "stuck")
		return false
	_best = left
	_best_t = _t
	_anchor = here
	_anchor_t = _t
	return true


## Starts wall-following: the steering direction is the wall's tangent,
## on the side that heads roughly toward the target.
func _enter_wall(here: Vector3) -> void:
	var normal := Vector3.ZERO
	for c in _body.get_slide_collision_count():
		var n := _body.get_slide_collision(c).get_normal()
		n.y = 0
		if n.length() > normal.length():
			normal = n
	if normal.length() < 0.01:
		normal = (here - _target)
		normal.y = 0
		normal = normal.normalized() if normal.length() > 0.01 else Vector3.FORWARD
	var tangent := Vector3(-normal.z, 0, normal.x).normalized()
	if tangent.dot(_target - here) < 0.0:
		tangent = -tangent
	if _wall_away:
		tangent = -tangent
	_wall_away = not _wall_away
	_wall_dir = tangent
	_wall_t = 0.0
	_wall_clear = 0.0
	_wall_flipped = false
	_wall_entries += 1
	_mode = "wall"


func _physics_process(delta: float) -> void:
	if _state == "settle":
		# The navigation map takes in the scene's regions asynchronously;
		# wait for its first iteration (or give up: a scene without a mesh
		# is walked direct).
		_settle -= 1
		var synced := _has_nav and NavigationServer3D.map_get_iteration_id(_map) > 0
		if _settle == 0 or (synced and _settle < 120 - 3) or (not _has_nav and _settle < 120 - 5):
			_state = "between"
			_next_leg.call_deferred()
		return
	if _state == "drop":
		# After a teleport, fall to the ground before the clock starts.
		_drop += 1
		_body.velocity.x = 0.0
		_body.velocity.z = 0.0
		_body.velocity.y = 0.0 if _body.is_on_floor() else _body.velocity.y - 20.0 * delta
		_body.move_and_slide()
		if _body.is_on_floor() or _drop > 150:
			_last = _body.global_position
			_state = "walk"
		return
	if _state != "walk":
		return
	_frames += 1
	_t += delta
	var here := _body.global_position
	_walked += _flat(here, _last)
	_last = here
	var left := _flat(here, _target)
	if left < _best - 0.5:
		_best = left
		_best_t = _t
	if _flat(here, _anchor) >= 0.4:
		_anchor = here
		_anchor_t = _t
	if left < ARRIVE:
		_end_leg(true)
		return
	# Two tripwires, one fallback chain: no distance progress for
	# STUCK_AFTER seconds, or no actual movement for 3 s (pressed against
	# something the distance metric still credits with micro-progress).
	# Wall mode runs on its own rules: moving along the wall is progress,
	# leaving the wall (climbed up, rounded the end) ends it, and a stall
	# reverses the direction once before the leg gives up.
	if _mode == "wall":
		_wall_t += delta
		if _body.is_on_wall():
			_wall_clear = 0.0
		else:
			_wall_clear += delta
			if _wall_clear > 0.6:
				_mode = "direct"
				_best = left
				_best_t = _t
				_anchor = here
				_anchor_t = _t
		if _mode == "wall":
			if _wall_t > WALL_LIMIT:
				_end_leg(false, "stuck")
				return
			if _t - _anchor_t > 3.0:
				if not _wall_flipped:
					_wall_dir = -_wall_dir
					_wall_flipped = true
					_anchor = here
					_anchor_t = _t
				elif _wall_entries < MAX_WALLS:
					# A pocket: out the way it came in, and the next
					# entry starts the other way along the wall.
					_enter_wall(here)
					_anchor = here
					_anchor_t = _t
					_best = left
					_best_t = _t
				else:
					_end_leg(false, "stuck")
					return
	elif _t - _best_t > STUCK_AFTER or _t - _anchor_t > 3.0:
		if not _blocked(here, left):
			return
	if _t > _leg_cap():
		_end_leg(false, "timeout")
		return
	# Re-plan: on the mesh, follow the navigation path; off it, steer direct.
	# "On the mesh" is height-aware: a mesh point a full step above the
	# walker's feet (the foundation slab's top, seen from the land below)
	# is not ground the walker is on.
	_replan -= 1
	if _replan <= 0:
		_replan = REPLAN
		if _has_nav:
			var closest := NavigationServer3D.map_get_closest_point(_map, here)
			var on_mesh := _flat(here, closest) <= 2.0 and closest.y - here.y <= STEP + 0.15 and here.y - closest.y <= 2.5
			if _mode == "seek":
				_seek_point = closest
				if on_mesh or _flat(here, _seek_point) < 1.0:
					_mode = "direct"  # Reached the network; re-plan below.
			if _mode != "seek" and _mode != "wall" and on_mesh and not _nav_done:
				var p := NavigationServer3D.map_get_path(_map, here, _target, true)
				if p.size() >= 2:
					_path = p
					_path_i = 0
					_mode = "nav"
					_used_nav = true
			elif _mode == "nav" and not on_mesh:
				_mode = "direct"
	var waypoint := _target
	if _mode == "wall":
		waypoint = here + _wall_dir * 4.0
	elif _mode == "seek":
		waypoint = _seek_point
	elif _mode == "nav" and not _path.is_empty():
		while _path_i < _path.size() - 1 and _flat(here, _path[_path_i]) < 0.3:
			_path_i += 1
			_best_t = _t  # Advancing along the path is progress too.
		if _path_i >= _path.size() - 1 and _flat(here, _path[_path.size() - 1]) < 0.5:
			# The path ran out short of the target (the target is off the
			# mesh: a roof, a shore): steer the rest directly.
			_mode = "direct"
			_nav_done = true
		else:
			waypoint = _path[_path_i]
	var dir := waypoint - here
	dir.y = 0
	if dir.length() > 0.01:
		dir = dir.normalized() * SPEED
	_body.velocity.x = dir.x
	_body.velocity.z = dir.z
	_body.velocity.y = 0.0 if _body.is_on_floor() else _body.velocity.y - 20.0 * delta
	var before := _body.global_position
	_body.move_and_slide()
	# Step up curbs: when blocked while walking, rise one step, but only if
	# the way on is free up there (a wall stays a wall).
	if _body.is_on_wall() and _flat(_body.global_position, before) < SPEED * delta * 0.3 and dir.length() > 0.01:
		var raised := _body.global_transform.translated(Vector3(0, STEP, 0))
		if not _body.test_move(raised, Vector3(dir.x, 0, dir.z).normalized() * 0.3):
			_body.global_position += Vector3(0, STEP, 0)
