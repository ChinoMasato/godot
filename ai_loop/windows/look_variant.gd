extends SceneTree
## High-quality U.ZU look with one change, to see what each setting costs and how it looks.
##   godot --path <sample> --rendering-driver vulkan --resolution 1920x1080 --script look_variant.gd -- --variant <name> [--frames 300] [--shot out.png]
## Variants: base, no_ssil, no_ssr, no_pcss, no_blend, no_ssao, shadow_filter_high, shadow_filter_ultra, pcss_025, shadow_blur_2, shadow_atlas_8k
## Prints "UZU_VARIANT {json}".

const WARMUP := 60

var _variant := "base"
var _frames := 300
var _shot := ""
var _frame := 0
var _gpu: Array[float] = []
var _vp: RID


func _opt(args: PackedStringArray, name: String, default: String) -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_variant = _opt(args, "variant", "base")
	_frames = _opt(args, "frames", "300").to_int()
	_shot = _opt(args, "shot", "")
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var scene := (load("res://main.tscn") as PackedScene).instantiate()
	UZULook.apply_to_scene(scene, UZULook.QUALITY_HIGH, UZULook.ATMOSPHERE_CLEAR, false)
	root.add_child(scene)
	var env: Environment = (scene.find_children("*", "WorldEnvironment", true, false)[0] as WorldEnvironment).environment
	var sun := scene.find_children("*", "DirectionalLight3D", true, false)[0] as DirectionalLight3D
	match _variant:
		"no_ssil": env.ssil_enabled = false
		"no_ssr": env.ssr_enabled = false
		"no_ssao": env.ssao_enabled = false
		"no_pcss": sun.light_angular_distance = 0.0
		"pcss_025": sun.light_angular_distance = 0.25
		"no_blend": sun.directional_shadow_blend_splits = false
		"shadow_blur_2": sun.shadow_blur = 2.0
		"shadow_filter_high": RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_HIGH)
		"shadow_filter_ultra": RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_ULTRA)
		"shadow_atlas_8k": RenderingServer.directional_shadow_atlas_set_size(8192, true)
		"taa": root.use_taa = true
		"pcss_025_filter_high":
			sun.light_angular_distance = 0.25
			RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_HIGH)
	_vp = root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(_vp, true)


func _process(_delta: float) -> bool:
	_frame += 1
	if _frame > WARMUP:
		_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(_vp))
	if _frame >= WARMUP + _frames:
		if not _shot.is_empty():
			root.get_texture().get_image().save_png(_shot)
		var s := _gpu.duplicate()
		s.sort()
		print("UZU_VARIANT " + JSON.stringify({"variant": _variant, "gpu_ms_median": snappedf(s[s.size() / 2], 0.001)}))
		return true
	return false
