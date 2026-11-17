extends SceneTree
## Measures GPU/CPU frame time of res://main.tscn with the U.ZU look at a given quality.
##   godot --path <sample> --rendering-driver vulkan --resolution 1920x1080 --script frame_time.gd -- --quality high|standard|low|none [--atmosphere clear] [--frames 300]
## Prints one line "UZU_FRAMETIME {json}".

const QUALITIES := {"high": UZULook.QUALITY_HIGH, "standard": UZULook.QUALITY_STANDARD, "low": UZULook.QUALITY_LOW}
const ATMOSPHERES := {"clear": UZULook.ATMOSPHERE_CLEAR, "humid": UZULook.ATMOSPHERE_HUMID, "golden_hour": UZULook.ATMOSPHERE_GOLDEN_HOUR, "night": UZULook.ATMOSPHERE_NIGHT}
const WARMUP := 60

var _quality := "high"
var _atmosphere := "clear"
var _frames := 300
var _frame := 0
var _gpu: Array[float] = []
var _cpu: Array[float] = []
var _vp: RID


func _opt(args: PackedStringArray, name: String, default: String) -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_quality = _opt(args, "quality", "high")
	_atmosphere = _opt(args, "atmosphere", "clear")
	_frames = _opt(args, "frames", "300").to_int()
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var scene := (load("res://main.tscn") as PackedScene).instantiate()
	if _quality != "none":
		UZULook.apply_to_scene(scene, QUALITIES[_quality], ATMOSPHERES[_atmosphere], false)
	root.add_child(scene)
	_vp = root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(_vp, true)


func _median(a: Array[float]) -> float:
	var s := a.duplicate()
	s.sort()
	return s[s.size() / 2] if s.size() > 0 else 0.0


func _percentile(a: Array[float], p: float) -> float:
	var s := a.duplicate()
	s.sort()
	return s[clampi(int(s.size() * p), 0, s.size() - 1)] if s.size() > 0 else 0.0


func _process(_delta: float) -> bool:
	_frame += 1
	if _frame > WARMUP:
		_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(_vp))
		_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(_vp) + RenderingServer.get_frame_setup_time_cpu())
	if _frame >= WARMUP + _frames:
		var result := {
			"quality": _quality, "atmosphere": _atmosphere, "frames": _gpu.size(),
			"size": str(root.get_visible_rect().size),
			"device": RenderingServer.get_video_adapter_name(),
			"gpu_ms_median": snappedf(_median(_gpu), 0.001), "gpu_ms_p95": snappedf(_percentile(_gpu, 0.95), 0.001),
			"cpu_ms_median": snappedf(_median(_cpu), 0.001),
		}
		print("UZU_FRAMETIME " + JSON.stringify(result))
		return true
	return false
