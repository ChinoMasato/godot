extends SceneTree
## Renders a scene as it is saved (no look applied here), measures the GPU frame time and saves the last frame.
## Use it after look commands (hd2d, particles, ...) have changed the scene.
##   godot --path <project> --rendering-driver vulkan --resolution 1920x1080 --script scene_time.gd -- [--scene res://main.tscn] [--frames 300] [--shot out.png] [--drop UZUTiltShift]
##   --drop removes compositor effects of that class first (to measure what one effect costs).
## Prints one line "UZU_SCENETIME {json}".

const WARMUP := 60

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
	_frames = _opt(args, "frames", "300").to_int()
	_shot = _opt(args, "shot", "")
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var packed := load(_opt(args, "scene", "res://main.tscn")) as PackedScene
	if packed == null:
		print("UZU_SCENETIME " + JSON.stringify({"ok": false, "error": "scene not found"}))
		quit(1)
		return
	var scene := packed.instantiate()
	var drop := _opt(args, "drop", "")
	if not drop.is_empty():
		for we in scene.find_children("*", "WorldEnvironment", true, false):
			var comp: Compositor = (we as WorldEnvironment).compositor
			if comp:
				var kept: Array[CompositorEffect] = []
				for e in comp.compositor_effects:
					if e and e.get_class() != drop:
						kept.append(e)
				comp.compositor_effects = kept
	root.add_child(scene)
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
		var size := root.get_texture().get_size()
		print("UZU_SCENETIME " + JSON.stringify({"ok": true, "gpu_ms_median": snappedf(s[s.size() / 2], 0.001),
				"gpu_ms_p95": snappedf(s[int(s.size() * 0.95)], 0.001), "size": [size.x, size.y]}))
		return true
	return false
