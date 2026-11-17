extends SceneTree
## Agent-facing command line for the U.ZU look (UZULook). Run through ai_loop/look.sh.
##
## Commands (after "--"):
##   apply    --scene res://main.tscn [--quality high|standard|low] [--atmosphere clear|humid|golden_hour|night] [--clouds]
##   rig      --scene res://main.tscn [--atmosphere ...]          adds fill and rim lights
##   lamp     --scene res://main.tscn --position x,y,z [--quality ...]
##   camera   --scene res://main.tscn [--node Path/To/Camera3D] [--lens wide|standard|telephoto] [--shift 0.2] [--eye standing|low]
##   material --kind skin|foliage|glass|water|lamp --out res://materials/water.tres
##   material --kind water --scene res://main.tscn --node Path/To/MeshInstance3D   assigns it as the material override
##   sprite   --scene res://main.tscn [--node Path/To/Sprite3D] [--normal res://n.png] [--outline 1] [--blob 0.4]
##            makes Sprite3D nodes lit pixel-art sprites (all of them without --node), with an optional blob shadow (B-716)
##   info     lists the options
## Prints one line "UZU_RESULT {json}" with {"ok": bool, ...}; exits 0 on success, 1 on error.

const QUALITIES := {"high": UZULook.QUALITY_HIGH, "standard": UZULook.QUALITY_STANDARD, "low": UZULook.QUALITY_LOW}
const ATMOSPHERES := {"clear": UZULook.ATMOSPHERE_CLEAR, "humid": UZULook.ATMOSPHERE_HUMID, "golden_hour": UZULook.ATMOSPHERE_GOLDEN_HOUR, "night": UZULook.ATMOSPHERE_NIGHT}
const MATERIALS := {"skin": UZULook.MATERIAL_SKIN, "foliage": UZULook.MATERIAL_FOLIAGE, "glass": UZULook.MATERIAL_GLASS, "water": UZULook.MATERIAL_WATER, "lamp": UZULook.MATERIAL_LAMP}
const LENSES := {"wide": UZULook.LENS_WIDE, "standard": UZULook.LENS_STANDARD, "telephoto": UZULook.LENS_TELEPHOTO}
const EYE_LEVELS := {"standing": UZULook.EYE_LEVEL_STANDING, "low": UZULook.EYE_LEVEL_LOW}


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var result := _run(args)
	print("UZU_RESULT " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


func _option(args: PackedStringArray, name: String, default: String = "") -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _pick(table: Dictionary, args: PackedStringArray, name: String, default: String) -> Variant:
	var key := _option(args, name, default)
	if not table.has(key):
		return null
	return table[key]


func _error(message: String) -> Dictionary:
	return {"ok": false, "error": message}


func _load_scene(path: String) -> Node:
	if path.is_empty():
		return null
	var packed := load(path) as PackedScene
	return packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE) if packed else null


func _save_scene(root: Node, path: String) -> Error:
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err == OK:
		err = ResourceSaver.save(packed, path)
	return err


func _run(args: PackedStringArray) -> Dictionary:
	if args.is_empty() or args[0] == "info":
		return {"ok": true, "commands": ["apply", "rig", "lamp", "camera", "material", "sprite", "shot", "check", "panorama", "bake", "info"], "quality": QUALITIES.keys(), "atmosphere": ATMOSPHERES.keys(), "material": MATERIALS.keys(), "lens": LENSES.keys(), "eye": EYE_LEVELS.keys()}
	var command := args[0]

	if command == "material" and _option(args, "scene").is_empty():
		var kind = _pick(MATERIALS, args, "kind", "")
		var out := _option(args, "out")
		if kind == null or out.is_empty():
			return _error("material needs --kind %s and --out res://path.tres (or --scene and --node to assign it)" % "|".join(MATERIALS.keys()))
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out.get_base_dir()))
		var err := ResourceSaver.save(UZULook.create_material(kind), out)
		return {"ok": err == OK, "command": command, "saved": out, "error": error_string(err) if err != OK else ""}

	var scene_path := _option(args, "scene")
	var root := _load_scene(scene_path)
	if root == null:
		return _error("could not load --scene '%s'" % scene_path)
	var quality = _pick(QUALITIES, args, "quality", "high")
	var atmosphere = _pick(ATMOSPHERES, args, "atmosphere", "clear")
	if quality == null or atmosphere == null:
		root.free()
		return _error("unknown --quality or --atmosphere")

	var done := {}
	match command:
		"apply":
			done = UZULook.apply_to_scene(root, quality, atmosphere, args.has("--clouds"))
		"rig":
			done = UZULook.add_lighting_rig(root, atmosphere)
		"lamp":
			var p := _option(args, "position").split(",")
			if p.size() != 3:
				root.free()
				return _error("lamp needs --position x,y,z")
			done = UZULook.add_lamp(root, Vector3(p[0].to_float(), p[1].to_float(), p[2].to_float()), quality)
		"camera":
			var node_path := _option(args, "node")
			var camera: Camera3D = root.get_node_or_null(node_path) as Camera3D if not node_path.is_empty() else null
			if camera == null:
				var found := root.find_children("*", "Camera3D", true, false)
				camera = found[0] as Camera3D if found.size() > 0 else null
			var lens = _pick(LENSES, args, "lens", "standard")
			if camera == null or lens == null:
				root.free()
				return _error("camera needs a Camera3D in the scene (or --node) and a valid --lens")
			UZULook.configure_camera(camera, lens, _option(args, "shift", "0").to_float())
			var eye := _option(args, "eye")
			if not eye.is_empty():
				if not EYE_LEVELS.has(eye):
					root.free()
					return _error("unknown --eye")
				camera.position.y = UZULook.get_eye_height(EYE_LEVELS[eye])
			done = {"camera": str(root.get_path_to(camera)), "fov": UZULook.get_lens_fov(lens), "projection": camera.projection, "height": camera.position.y}
		"material":
			var kind = _pick(MATERIALS, args, "kind", "")
			var target := root.get_node_or_null(_option(args, "node")) as GeometryInstance3D
			if kind == null or target == null:
				root.free()
				return _error("material needs --kind and --node pointing to a GeometryInstance3D")
			target.material_override = UZULook.create_material(kind)
			done = {"node": str(root.get_path_to(target)), "material": _option(args, "kind")}
		"sprite":
			var sprites := []
			var node_path := _option(args, "node")
			if not node_path.is_empty():
				var one := root.get_node_or_null(node_path) as Sprite3D
				if one:
					sprites.append(one)
			else:
				sprites = root.find_children("*", "Sprite3D", true, false)
			if sprites.is_empty():
				root.free()
				return _error("sprite needs Sprite3D nodes in the scene (or --node pointing to one)")
			var normal_path := _option(args, "normal")
			var normal: Texture2D = load(normal_path) as Texture2D if not normal_path.is_empty() else null
			if not normal_path.is_empty() and normal == null:
				root.free()
				return _error("could not load --normal '%s'" % normal_path)
			var blob := _option(args, "blob").to_float()
			var names := []
			for s in sprites:
				UZULook.configure_lit_sprite(s, normal, _option(args, "outline", "0").to_float())
				if blob > 0.0 and s.get_node_or_null("BlobShadow") == null:
					UZULook.add_blob_shadow(s, blob)
				names.append(str(root.get_path_to(s)))
			done = {"sprites": names, "outline": _option(args, "outline", "0").to_float(), "blob": blob, "normal": normal_path}
		_:
			root.free()
			return _error("unknown command '%s'" % command)

	var err := _save_scene(root, scene_path)
	root.free()
	if err == OK and command == "apply":
		err = ProjectSettings.save()  # The sun's shadow filter quality is a project setting (B-714).
	if err != OK:
		return _error("could not save '%s': %s" % [scene_path, error_string(err)])
	done["ok"] = true
	done["command"] = command
	done["scene"] = scene_path
	return done
