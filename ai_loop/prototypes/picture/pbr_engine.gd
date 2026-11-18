extends SceneTree
## Agent command line (B-902): one texture image → PBR maps, computed by the
## engine's UZUPBRFromImage (the C++ port of pbr_tool.gd; the same fitted kind
## table, no per-pixel GDScript loop).
##   godot --headless --path P --script pbr_engine.gd -- make --in res://tex.png --out res://materials/stone [--kind stone]
##        [--strength 1.0] [--delight 0.7] [--tile] [--invert] [--normal-from res://n.png] [--size 1024]
##   godot --headless --path P --script pbr_engine.gd -- kinds
## Prints "UZU_RESULT {json}" like the other picture tools.

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var res := _run(args)
	print("UZU_RESULT " + JSON.stringify(res))
	quit(0 if res.get("ok", false) else 1)

func _opt(args: PackedStringArray, name: String, default: String = "") -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default

func _run(args: PackedStringArray) -> Dictionary:
	if args.is_empty():
		return {"ok": false, "error": "command needed: make | kinds"}
	match args[0]:
		"kinds":
			return {"ok": true, "kinds": UZUPBRFromImage.kinds()}
		"make":
			var in_path := _opt(args, "in")
			var out_dir := _opt(args, "out")
			if in_path.is_empty() or out_dir.is_empty():
				return {"ok": false, "error": "make needs --in res://texture.png and --out res://materials/name"}
			return UZUPBRFromImage.make_file(in_path, out_dir, _opt(args, "kind", "stone"),
				_opt(args, "strength", "1.0").to_float(), _opt(args, "delight", "0.7").to_float(),
				args.has("--tile"), args.has("--invert"), _opt(args, "normal-from"),
				_opt(args, "size", "1024").to_int())
	return {"ok": false, "error": "unknown command"}
