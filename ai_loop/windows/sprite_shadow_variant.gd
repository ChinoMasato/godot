extends SceneTree
## Renders the saved (look hd2d applied) scene with one change to find where the dark band on the sprites comes from.
##   godot --path <hd2d copy after look hd2d> --rendering-driver vulkan --resolution 1920x1080 --script sprite_shadow_variant.gd -- --variant <name> --shot out.png
## Variants: base, no_cast, only_hero_casts, only_hero2_casts, only_hero3_casts, sun_off, bias_hi, normal_bias_hi,
## backlight_0, no_ssao, no_ssil, no_ssr, no_glow, no_fog, no_screen_fx, no_blob, no_tiltshift. Prints "BAND {json}".

var _frame := 0
var _shot := ""


func _opt(args: PackedStringArray, name: String, default: String) -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_shot = _opt(args, "shot", "out.png")
	var scene := (load("res://main.tscn") as PackedScene).instantiate()
	var sprites := scene.find_children("*", "Sprite3D", true, false)
	var suns := scene.find_children("*", "DirectionalLight3D", true, false)
	var variant := _opt(args, "variant", "base")
	match variant:
		"no_cast":
			for s in sprites:
				(s as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		"only_hero_casts", "only_hero2_casts", "only_hero3_casts":
			var keep: String = {"only_hero_casts": "Hero", "only_hero2_casts": "Hero2", "only_hero3_casts": "Hero3"}[variant]
			for s in sprites:
				if s.name != keep:
					(s as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		"sun_off":
			for l in suns:
				(l as DirectionalLight3D).shadow_enabled = false
		"bias_hi":
			for l in suns:
				(l as DirectionalLight3D).shadow_bias = 0.5
		"normal_bias_hi":
			for l in suns:
				(l as DirectionalLight3D).shadow_normal_bias = 4.0
		"backlight_0":
			for s in sprites:
				var m := (s as GeometryInstance3D).material_override as ShaderMaterial
				if m:
					m.set_shader_parameter("backlight", 0.0)
		"no_ssao", "no_ssil", "no_ssr", "no_glow", "no_fog", "no_screen_fx":
			for we in scene.find_children("*", "WorldEnvironment", true, false):
				var env := (we as WorldEnvironment).environment
				if variant == "no_ssao" or variant == "no_screen_fx": env.ssao_enabled = false
				if variant == "no_ssil" or variant == "no_screen_fx": env.ssil_enabled = false
				if variant == "no_ssr" or variant == "no_screen_fx": env.ssr_enabled = false
				if variant == "no_glow" or variant == "no_screen_fx": env.glow_enabled = false
				if variant == "no_fog" or variant == "no_screen_fx":
					env.fog_enabled = false
					env.volumetric_fog_enabled = false
				if variant == "no_screen_fx": (we as WorldEnvironment).compositor = null
		"no_blob":
			for d in scene.find_children("BlobShadow", "Decal", true, false):
				d.queue_free()
		"no_tiltshift":
			for we in scene.find_children("*", "WorldEnvironment", true, false):
				(we as WorldEnvironment).compositor = null
	root.add_child(scene)
	var info := {"variant": variant, "sprites": sprites.size(), "suns": suns.size()}
	if suns.size() > 0:
		var l := suns[0] as DirectionalLight3D
		info["bias"] = l.shadow_bias
		info["normal_bias"] = l.shadow_normal_bias
		info["angular"] = l.light_angular_distance
	var blobs := scene.find_children("*", "Decal", true, false)
	if blobs.size() > 0:
		var dc := blobs[0] as Decal
		info["blob"] = {"count": blobs.size(), "size": str(dc.size), "pos": str(dc.global_position if dc.is_inside_tree() else dc.position), "cull_mask": dc.cull_mask}
	print("BAND " + JSON.stringify(info))


func _process(_delta: float) -> bool:
	_frame += 1
	if _frame == 90:
		root.get_texture().get_image().save_png(_shot)
		return true
	return false
