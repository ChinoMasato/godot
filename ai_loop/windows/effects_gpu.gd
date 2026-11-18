extends SceneTree
## GPU regression fixture for B-719/B-720. Shader TIME can be pinned for snapshots.
## --case wind|rays --strength 0.12 --time 0.4 --shot path.png --frames 180

var frame := 0
var timings: Array[float] = []
var shot := ""
var frames := 180
var case_name := "wind"
var pinned_time := 0.0
var strength := 0.12


func option(name: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	var index := args.find("--" + name)
	return args[index + 1] if index >= 0 and index + 1 < args.size() else fallback


func _initialize() -> void:
	call_deferred("build")


func build() -> void:
	case_name = option("case", "wind")
	strength = option("strength", "0.12").to_float()
	pinned_time = option("time", "0.4").to_float()
	frames = maxi(1, option("frames", "180").to_int())
	shot = option("shot", "")
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var world := Node3D.new()
	root.add_child(world)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.055, 0.075, 0.10)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.7, 0.8, 1.0)
	environment.ambient_light_energy = 0.4
	var we := WorldEnvironment.new()
	we.environment = environment
	world.add_child(we)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 5.5
	world.add_child(camera)
	camera.position = Vector3(0, 2.2, 7)
	camera.look_at(Vector3(0, 0.9, 0))
	var sun := DirectionalLight3D.new()
	world.add_child(sun)
	sun.rotation_degrees = Vector3(-40, -60, 0)
	sun.light_color = Color(1, 0.85, 0.65)
	sun.light_energy = 1.4
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(12, 12)
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.19, 0.22, 0.24)
	add_mesh(world, floor_mesh, floor_material, Vector3.ZERO)
	if case_name == "wind":
		for x in [-1.5, 0.0, 1.5]:
			var mesh := CylinderMesh.new()
			mesh.height = 1.5
			mesh.top_radius = 0.05
			mesh.bottom_radius = 0.32
			mesh.radial_segments = 16
			mesh.rings = 12
			var material := UZULook.create_wind_foliage_material(strength, 1.2)
			# Pin only TIME in a copy of the production shader; all wind math remains intact.
			if pinned_time >= 0:
				var shader := Shader.new()
				shader.code = material.shader.code.replace("void vertex()", "uniform float capture_time = 0.0;\nvoid vertex()").replace("TIME", "capture_time")
				material.shader = shader
				material.set_shader_parameter("capture_time", pinned_time)
			add_mesh(world, mesh, material, Vector3(x, 0.75, 0))
	elif case_name == "rays":
		if strength > 0:
			UZULook.add_god_rays(world, Vector3(-1, 2.5, 0), 1.5, 4.5)
			UZULook.add_god_rays(world, Vector3(1.3, 3.0, -1), 0.8, 5.0)
	else:
		push_error("Unknown case: " + case_name)
		quit(1)
		return
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)


func add_mesh(parent: Node3D, mesh: Mesh, material: Material, position: Vector3) -> void:
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = position
	parent.add_child(instance)


func _process(_delta: float) -> bool:
	frame += 1
	if frame == 60 and pinned_time < 0:
		capture_start.call_deferred()
	if frame > 60:
		timings.append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
	if frame == frames + 60:
		capture.call_deferred()
	return false


func capture_start() -> void:
	await RenderingServer.frame_post_draw
	var error := root.get_texture().get_image().save_png(shot.get_basename() + "_start.png")
	if error != OK:
		push_error("Failed to save start frame")
		quit(1)


func capture() -> void:
	await RenderingServer.frame_post_draw
	var result := root.get_texture().get_image()
	var error := result.save_png(shot)
	timings.sort()
	print("UZU_EFFECTS " + JSON.stringify({"ok": error == OK, "case": case_name,
		"strength": strength, "time": pinned_time, "image": shot,
		"gpu_ms_median": timings[timings.size() / 2], "device": RenderingServer.get_video_adapter_name(),
		"size": [result.get_width(), result.get_height()]}))
	quit(0 if error == OK else 1)
