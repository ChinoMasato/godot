extends Node3D
## UZUVfx sample (BACKLOG B-1109): a tiny scene that fires the built-in
## explosion and a laser built from a recipe dictionary, for the screenshot
## flow (ai_loop/uzu.py look <this project> shot --frames 13).
##
## The firing is keyed to frame numbers, not seconds: effect lifetimes are
## wall-clock based (like UZUJuice's), while a software-rendered capture
## frame can take a second or more of wall time. Firing both effects again
## in the last frames before the capture keeps them mid-flight in the shot.
## In a running game this is unnecessary — fire once, when things happen.

var _frame := 0
var _anchor: Node3D


func _ready() -> void:
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(30, 30)
	var gmat := StandardMaterial3D.new()
	gmat.albedo_color = Color(0.16, 0.15, 0.14)
	gmat.roughness = 0.95
	plane.material = gmat
	ground.mesh = plane
	add_child(ground)
	for spec in [[-4.5, -3.0, 2.0], [4.2, -3.5, 3.2], [0.5, -5.0, 1.4]]:
		var box := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(1.8, spec[2], 1.8)
		var bmat := StandardMaterial3D.new()
		bmat.albedo_color = Color(0.35, 0.3, 0.28)
		bm.material = bmat
		box.mesh = bm
		box.position = Vector3(spec[0], spec[2] / 2.0, spec[1])
		add_child(box)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -30, 0)
	sun.light_energy = 1.1
	add_child(sun)
	var we := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.25, 0.35, 0.55)
	sky_mat.sky_horizon_color = Color(0.65, 0.6, 0.55)
	sky.sky_material = sky_mat
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	we.environment = env
	add_child(we)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 3.0, 8.5)
	add_child(camera)
	camera.look_at(Vector3(0, 0.9, 0), Vector3.UP)
	camera.current = true
	_anchor = Node3D.new()
	add_child(_anchor)


func _process(_delta: float) -> void:
	_frame += 1
	if _frame == 8 or _frame == 10 or _frame == 12:
		UZUVfx.fire_named(_anchor, Vector3(-1.6, 0.15, 0), "explosion")
	if _frame == 9 or _frame == 11:
		# The laser recipe's shape, with a longer beam so a screenshot can
		# catch it (the built-in beam is a 0.18 s flash).
		var recipe := {"name": "laser_demo", "parts": [
			{"type": "beam", "duration": 2.0},
			{"type": "sparks", "at": "target", "count": 18, "color": [0.6, 0.95, 1.0], "lifetime": 0.5},
			{"type": "light", "at": "target", "color": [0.4, 0.8, 1.0], "energy": 3.0, "range": 4.0, "duration": 2.0},
			{"type": "sfx", "preset": "laser"}]}
		UZUVfx.fire_between(_anchor, Vector3(3.4, 2.6, -1.2), Vector3(-1.6, 0.9, 0), recipe)
