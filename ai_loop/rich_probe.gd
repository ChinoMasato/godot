extends Node
## Rendered-frame probe for the richness report (BACKLOG B-1204). Installed as an
## autoload by `ai_loop/uzu.py look rich` in a copy of the project. After the given
## number of frames it reads what the renderer drew in the last frame and writes it
## to the file given by "--uzu-rich <file> <frames>" after "--", then quits:
##   {"frame_triangles", "frame_draw_calls", "frame_objects" (the main pass),
##    "frame_shadow_triangles", "frame_shadow_draw_calls" (shadow maps),
##    "video_mb", "texture_mb_used", "buffer_mb_used", "frames"}

var _path := ""
var _frames := 10
var _seen := 0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-rich")
	if i < 0 or i + 2 >= args.size():
		set_process(false)
		return
	_path = args[i + 1]
	_frames = maxi(2, args[i + 2].to_int())


func _process(_delta: float) -> void:
	_seen += 1
	if _seen < _frames:
		return
	set_process(false)
	var vp := get_viewport()
	var mb := func(bytes: int) -> float: return snappedf(bytes / 1048576.0, 0.01)
	var info := {
		"frames": _seen,
		"frame_triangles": vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME),
		"frame_draw_calls": vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
		"frame_objects": vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_OBJECTS_IN_FRAME),
		"frame_shadow_triangles": vp.get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME),
		"frame_shadow_draw_calls": vp.get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
		"video_mb": mb.call(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED)),
		"texture_mb_used": mb.call(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED)),
		"buffer_mb_used": mb.call(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_BUFFER_MEM_USED)),
	}
	var f := FileAccess.open(_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(info))
		f.close()
	get_tree().quit()
