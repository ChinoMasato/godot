## B-1507 self-check: an AI walker crosses a generated city on its NavigationMesh.
##   godot --headless --script ai_loop/city_walk_check.gd -- '{"preset": "town", "seed": 3}'
## Prints WALK_RESULT {"ok", "legs": [{"to", "reached", "frames", "path_points", "left_m"}], "navigation"}
## and exits 1 when any leg (spawn to each gate and the plaza) is not reached.
extends SceneTree
# B-1507 self-check: build a town, let the navigation map sync, then walk an agent from the
# spawn to every gate and the plaza along NavigationServer3D paths, with a CharacterBody3D
# moved by physics so buildings and benches really block it.

var city_result: Dictionary
var targets := []
var agent: CharacterBody3D
var path := PackedVector3Array()
var path_i := 0
var target_i := -1
var frames := 0
var leg_frames := 0
var report := []
var root3d: Node3D

func _initialize() -> void:
	NavigationServer3D.set_active(true)
	root3d = Node3D.new()
	get_root().add_child(root3d)
	var spec := {"preset": "town", "seed": 3, "size": 160, "layout": "organic"}
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		spec = JSON.parse_string(args[0])
	city_result = UZUWorldGen.generate_city(root3d, spec)
	if not city_result.ok:
		print("WALK_RESULT " + JSON.stringify(city_result))
		quit(1)
		return
	var p: Dictionary = city_result.places
	for name in ["gate_north", "gate_east", "gate_south", "gate_west", "plaza"]:
		if p.has(name):
			targets.append([name, _v(p[name].position)])
	agent = CharacterBody3D.new()
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.35
	capsule.height = 1.7
	shape.shape = capsule
	shape.position.y = 0.85
	agent.add_child(shape)
	agent.floor_snap_length = 0.4
	agent.floor_max_angle = deg_to_rad(65) # a 0.35 m capsule meets a 0.15 m curb at 55 degrees
	root3d.add_child(agent)
	agent.position = _v(p.spawn.position)

func _v(a) -> Vector3:
	return Vector3(a[0], a[1], a[2])

func _physics_process(delta: float) -> bool:
	frames += 1
	var m := agent.get_world_3d().navigation_map
	# An empty map answers (0, 0, 0), so also wait for the first sync that has the region.
	if frames < 5 or NavigationServer3D.map_get_iteration_id(m) < 2 or NavigationServer3D.map_get_closest_point(m, agent.position).distance_to(agent.position) > 3.0:
		if frames > 600:
			print("WALK_RESULT " + JSON.stringify({"ok": false, "error": "the navigation map never synced"}))
			quit(1)
			return true
		return false # the map syncs in the first frames
	if target_i == -1 or path_i >= path.size():
		if target_i >= 0:
			var end: Vector3 = targets[target_i][1]
			report.append({"to": targets[target_i][0], "reached": agent.position.distance_to(end) < 2.5, "frames": leg_frames, "path_points": path.size(), "left_m": snappedf(agent.position.distance_to(end), 0.01)})
		target_i += 1
		if target_i >= targets.size():
			var ok := true
			for r in report:
				ok = ok and r.reached
			print("WALK_RESULT " + JSON.stringify({"ok": ok, "legs": report, "navigation": city_result.get("navigation", {}), "benches": city_result.counts.benches}))
			quit(0 if ok else 1)
			return true
		var map := agent.get_world_3d().navigation_map
		path = NavigationServer3D.map_get_path(map, agent.position, targets[target_i][1], true)
		path_i = 1
		leg_frames = 0
		if path.size() < 2:
			path_i = path.size()
		return false
	leg_frames += 1
	if leg_frames > 60 * 120:
		path_i = path.size() # stuck: give up on this leg
		return false
	var goal := path[path_i]
	var to := goal - agent.position
	to.y = 0
	if to.length() < 0.5:
		path_i += 1
		return false
	agent.velocity = to.normalized() * 4.0 + Vector3(0, agent.velocity.y - 9.8 * delta, 0)
	agent.move_and_slide()
	return false
