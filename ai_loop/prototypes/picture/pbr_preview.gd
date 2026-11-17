extends SceneTree
## Renders materials side by side on a sphere and a tiled floor under a low raking light, so bumps,
## shine and seams are visible. Run without --headless (it needs the GPU):
##   godot --path P --rendering-driver vulkan --script pbr_preview.gd -- --dirs res://a,res://b --out out.png [--labels "ours,scan"]
## A dir holds albedo.png / normal.png / orm.png (from pbr_tool.gd make), or pass "ref:<albedo>|<normal>|<arm>".

var _frames := 0
var _out := ""


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--dirs")
	var dirs: PackedStringArray = args[i + 1].split(",") if i >= 0 else PackedStringArray()
	var o := args.find("--out")
	_out = args[o + 1] if o >= 0 else "preview.png"
	root.size = Vector2i(560 * maxi(1, dirs.size()), 560)
	var world := Node3D.new()
	root.add_child(world)
	for k in dirs.size():
		var m := _material(dirs[k])
		var x := (k - (dirs.size() - 1) * 0.5) * 3.2
		var sphere := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 1.0
		sm.height = 2.0
		sm.radial_segments = 96
		sm.rings = 48
		sphere.mesh = sm
		sphere.material_override = m
		sphere.position = Vector3(x, 1.0, 0)
		world.add_child(sphere)
		var floor_mi := MeshInstance3D.new()
		var pm := PlaneMesh.new()
		pm.size = Vector2(3.0, 3.0)
		floor_mi.mesh = pm
		var fm: ORMMaterial3D = m.duplicate()
		fm.uv1_scale = Vector3(2, 2, 1)
		floor_mi.material_override = fm
		floor_mi.position = Vector3(x, 0, 0.4)
		world.add_child(floor_mi)
	var sun := DirectionalLight3D.new()
	sun.light_energy = 2.2
	sun.shadow_enabled = true
	world.add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, Vector3(-1.0, -0.35, -0.25), Vector3.UP)  # low raking light
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.08, 0.09, 0.11)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.35, 0.4, 0.5)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	var we := WorldEnvironment.new()
	we.environment = env
	world.add_child(we)
	var cam := Camera3D.new()
	cam.fov = 40
	world.add_child(cam)
	cam.look_at_from_position(Vector3(0, 3.0, 4.2 + dirs.size() * 0.9), Vector3(0, 0.6, 0), Vector3.UP)
	cam.make_current()


func _tex(path: String, srgb: bool) -> ImageTexture:
	var img := Image.load_from_file(ProjectSettings.globalize_path(path))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _material(spec: String) -> ORMMaterial3D:
	var m := ORMMaterial3D.new()
	var a: String
	var n: String
	var r: String
	if spec.begins_with("ref:"):
		var p := spec.substr(4).split("|")
		a = p[0]
		n = p[1]
		r = p[2]
	else:
		a = spec + "/albedo.png"
		n = spec + "/normal.png"
		r = spec + "/orm.png"
	m.albedo_texture = _tex(a, true)
	m.normal_enabled = true
	m.normal_texture = _tex(n, false)
	m.orm_texture = _tex(r, false)
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return m


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == 30:
		root.get_texture().get_image().save_png(_out)
		print("UZU_RESULT " + JSON.stringify({"ok": true, "image": _out}))
		return true
	return false
