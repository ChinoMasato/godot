extends SceneTree
## Declarative scene building (BACKLOG B-1207): a whole scene from one JSON spec, so an
## agent describes what it wants instead of stacking node operations. Run through
## `ai_loop/uzu.py build <project> --spec spec.json`, which also returns the richness
## report (B-1204) of the result.
##
##   godot --headless --path P --script build_tool.gd -- --spec /abs/spec.json
##
## Spec (every key is optional except "scene"; positions are [x, y, z] in meters,
## colors "#rrggbb"; an unknown key is an error that names where it is):
##   {"scene": "res://village.tscn", "main": true,
##    "base": "res://existing.tscn"   (add to an existing scene instead of starting empty; "scene"
##                                    defaults to it; re-applying replaces nodes of the same name),
##    "look": {"quality": "standard", "atmosphere": "golden_hour", "clouds": true, "rig": true},
##    "ground": {"size": [60, 60], "color": "#5a6b3a"},
##    "camera": {"position": [0, 8, 20], "look_at": [0, 0, 0], "lens": "standard"},
##    "models": [{"mesh": "box" | "sphere" | "cylinder" | "capsule" | "prism" | "res://house.glb",
##                "name": "House", "position": [...], "rotation_y": 30, "scale": 2 | [x, y, z],
##                "color": "#...", "material": "res://m.tres" | "foliage" | "skin" | ...}],
##    "scatter": [{"mesh": ..., "on": "Ground", "density": 0.5, "seed": 1, "scale": [0.2, 0.4],
##                 "color": "#...", "material": ..., "min_distance": 0, "max_slope": 90, "align": 0,
##                 "exclude": [{"center": [x, y, z], "radius": r}, {"aabb": [x, y, z, sx, sy, sz]}],
##                 "avoid": 0.5 (keep this far from the other objects' footprints),
##                 "near_camera": 30 (only a square this wide in front of the camera, for huge grounds;
##                 flat grounds), "shadows": true, "name": "Stones"}],   mesh "blob" is a 24-triangle sphere
##    "lamps": [[x, y, z], ...],
##    "particles": [{"kind": "fireflies", "position": [...] | "camera", "extents": [x, y, z]}],
##    "water": [{"position": [...], "size": [x, z], "shallow": "#...", "deep": "#...", "clarity": 2, "foam": 0.3}],
##    "godrays": [{"position": [...], "width": 3, "length": 8}]}
## Prints "BUILD_RESULT {json}" with what was built.

const QUALITIES := {"high": UZULook.QUALITY_HIGH, "standard": UZULook.QUALITY_STANDARD, "low": UZULook.QUALITY_LOW}
const ATMOSPHERES := {"clear": UZULook.ATMOSPHERE_CLEAR, "humid": UZULook.ATMOSPHERE_HUMID, "golden_hour": UZULook.ATMOSPHERE_GOLDEN_HOUR, "night": UZULook.ATMOSPHERE_NIGHT}
const MATERIALS := {"skin": UZULook.MATERIAL_SKIN, "foliage": UZULook.MATERIAL_FOLIAGE, "glass": UZULook.MATERIAL_GLASS, "water": UZULook.MATERIAL_WATER, "lamp": UZULook.MATERIAL_LAMP}
const LENSES := {"wide": UZULook.LENS_WIDE, "standard": UZULook.LENS_STANDARD, "telephoto": UZULook.LENS_TELEPHOTO}
const PARTICLES := {"dust": UZULook.PARTICLES_DUST, "fireflies": UZULook.PARTICLES_FIREFLIES, "snow": UZULook.PARTICLES_SNOW, "embers": UZULook.PARTICLES_EMBERS}
const KEYS := {
	"": ["scene", "base", "main", "look", "ground", "camera", "models", "scatter", "lamps", "particles", "water", "godrays"],
	"look": ["quality", "atmosphere", "clouds", "rig"],
	"ground": ["size", "color", "material"],
	"camera": ["position", "look_at", "lens", "fov"],
	"models": ["mesh", "name", "position", "rotation_y", "scale", "color", "material"],
	"scatter": ["mesh", "on", "density", "seed", "scale", "color", "material", "min_distance", "max_slope", "align", "sink", "max_count", "exclude", "name", "random_yaw", "avoid", "near_camera", "shadows"],
	"particles": ["kind", "position", "extents"],
	"water": ["position", "size", "shallow", "deep", "clarity", "waves", "wave_scale", "wave_speed", "swell", "foam", "refraction", "reflection"],
	"godrays": ["position", "width", "length"],
}

var errors := []


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--spec")
	var result := {}
	if i < 0 or i + 1 >= args.size():
		result = {"ok": false, "error": "build needs --spec spec.json"}
	else:
		result = _build(args[i + 1])
	print("BUILD_RESULT " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


func _err(where: String, message: String) -> void:
	errors.append("%s: %s" % [where, message])


func _check_keys(where: String, section: String, d) -> bool:
	if not d is Dictionary:
		_err(where, "must be an object")
		return false
	for key in d:
		if not KEYS[section].has(key):
			_err(where, "unknown key '%s' (allowed: %s)" % [key, ", ".join(KEYS[section])])
	return true


func _vec3(where: String, v, default := Vector3.ZERO) -> Vector3:
	if v == null:
		return default
	if v is Array and v.size() == 3:
		return Vector3(v[0], v[1], v[2])
	_err(where, "needs [x, y, z]")
	return default


func _color(where: String, v) -> Color:
	if v is String and Color.html_is_valid(v):
		return Color.html(v)
	_err(where, "needs a color like \"#5a6b3a\"")
	return Color.WHITE


## A primitive by name, a Mesh resource, or the first mesh of a scene (GLB) - for scatter.
func _mesh(where: String, spec) -> Mesh:
	var primitives := {"box": BoxMesh, "sphere": SphereMesh, "cylinder": CylinderMesh, "capsule": CapsuleMesh, "prism": PrismMesh, "plane": PlaneMesh}
	if spec is String and spec == "blob":  # A very low-poly sphere (24 triangles) for tiny props: flowers, pebbles.
		var blob := SphereMesh.new()
		blob.radial_segments = 6
		blob.rings = 3
		return blob
	if spec is String and primitives.has(spec):
		var m: PrimitiveMesh = primitives[spec].new()
		if m is SphereMesh:  # Low-poly by default: props are many (see look rich).
			m.radial_segments = 12
			m.rings = 6
		elif m is CylinderMesh or m is CapsuleMesh:
			m.radial_segments = 12
		return m
	if spec is String and ResourceLoader.exists(spec):
		var res := load(spec)
		if res is Mesh:
			return res
		if res is PackedScene:
			var inst: Node = res.instantiate()
			var found: Mesh = null
			for n in [inst] + inst.find_children("*", "MeshInstance3D", true, false):
				if n is MeshInstance3D and n.mesh != null:
					found = n.mesh
					break
			inst.free()
			if found:
				return found
	_err(where, "needs box, sphere, blob, cylinder, capsule, prism, plane, or a res:// mesh or scene")
	return null


## A color, a look material kind (foliage, skin, ...) or a res:// material.
func _material(where: String, d: Dictionary) -> Material:
	if d.has("material"):
		var m = d.material
		if m is String and MATERIALS.has(m):
			var preset: StandardMaterial3D = UZULook.create_material(MATERIALS[m])
			if d.has("color"):
				preset.albedo_color = _color(where + ".color", d.color)
			return preset
		if m is String and ResourceLoader.exists(m) and load(m) is Material:
			return load(m)
		_err(where + ".material", "needs %s or a res:// material" % ", ".join(MATERIALS.keys()))
		return null
	if d.has("color"):
		var mat := StandardMaterial3D.new()
		mat.albedo_color = _color(where + ".color", d.color)
		mat.roughness = 0.9
		return mat
	return null


func _to_root(root: Node, n: Node) -> Transform3D:
	var t := Transform3D()
	while n != null and n != root:
		if n is Node3D:
			t = n.transform * t
		n = n.get_parent()
	return t


## Exclusion boxes around the other objects (not the surface, not ground-sized things,
## not scattered props), grown by margin, so props stay out of houses and trees.
func _footprints(root: Node, surface: MeshInstance3D, margin: float) -> Array:
	var zones := []
	var ground := _to_root(root, surface) * surface.get_aabb()
	for n in root.find_children("*", "GeometryInstance3D", true, false):
		if n == surface or n is MultiMeshInstance3D or n is GPUParticles3D or n is CPUParticles3D:
			continue
		var local: AABB = n.get_aabb() if not n is MeshInstance3D or n.mesh == null else n.mesh.get_aabb()
		var box := _to_root(root, n) * local
		if box.size.x * box.size.z > ground.size.x * ground.size.z * 0.25:
			continue  # Ground-like.
		zones.append({"aabb": box.grow(margin)})
	return zones


## A flat square patch of the surface in front of the camera (at the surface's height), to
## scatter on instead of a huge ground. Freed after use.
func _camera_patch(root: Node, surface: MeshInstance3D, size: float) -> MeshInstance3D:
	var cams := root.find_children("*", "Camera3D", true, false)
	if cams.is_empty():
		return null
	var cam_t := _to_root(root, cams[0])
	var forward := -cam_t.basis.z
	forward.y = 0.0
	forward = forward.normalized() if forward.length() > 0.01 else Vector3(0, 0, -1)
	var center := cam_t.origin + forward * size * 0.45
	var ground := _to_root(root, surface) * surface.get_aabb()
	center.y = ground.end.y
	# Stay on the surface.
	center.x = clampf(center.x, ground.position.x + size * 0.5, ground.end.x - size * 0.5) if ground.size.x > size else ground.get_center().x
	center.z = clampf(center.z, ground.position.z + size * 0.5, ground.end.z - size * 0.5) if ground.size.z > size else ground.get_center().z
	var plane := PlaneMesh.new()
	plane.size = Vector2(minf(size, ground.size.x), minf(size, ground.size.z))
	var patch := MeshInstance3D.new()
	patch.mesh = plane
	patch.position = center
	root.add_child(patch)
	return patch


func _list(spec: Dictionary, key: String) -> Array:
	var v = spec.get(key, [])
	if not v is Array:
		_err(key, "must be a list")
		return []
	return v


func _build(spec_path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(spec_path if not spec_path.begins_with("res://") else spec_path)
	var spec = JSON.parse_string(text)
	if not spec is Dictionary:
		return {"ok": false, "error": "the spec is not a JSON object (%s)" % spec_path}
	_check_keys("spec", "", spec)
	var out: String = spec.get("scene", "")
	if not out.begins_with("res://") or not out.ends_with(".tscn"):
		_err("scene", "needs a res://...tscn path")
	var built := {}
	var root: Node3D = null
	if spec.has("base"):
		var base: String = spec.base
		var packed := load(base) as PackedScene if ResourceLoader.exists(base) else null
		var inst: Node = packed.instantiate(PackedScene.GEN_EDIT_STATE_MAIN) if packed else null
		if not inst is Node3D:
			if inst:
				inst.free()
			return {"ok": false, "error": "base needs a res:// scene whose root is a Node3D", "problems": ["base: no such 3D scene '%s'" % base]}
		root = inst
		if not spec.has("scene"):
			out = base
			errors = errors.filter(func(e): return not e.begins_with("scene:"))
		built["base"] = base
	else:
		root = Node3D.new()
		root.name = out.get_file().get_basename().to_pascal_case() if out else "Main"

	# Ground first: scatter and the look refer to it.
	if spec.has("ground") and _check_keys("ground", "ground", spec.ground):
		var g: Dictionary = spec.ground
		var size: Array = g.get("size", [40, 40])
		var plane := PlaneMesh.new()
		plane.size = Vector2(size[0], size[1])
		var ground := MeshInstance3D.new()
		ground.name = "Ground"
		ground.mesh = plane
		ground.material_override = _material("ground", g) if (g.has("color") or g.has("material")) else null
		root.add_child(ground)
		ground.owner = root
		built["ground"] = {"size": size}

	var models := _list(spec, "models")
	for k in models.size():
		var where := "models[%d]" % k
		var d = models[k]
		if not _check_keys(where, "models", d):
			continue
		var node: Node3D = null
		var mesh_spec = d.get("mesh", "box")
		if mesh_spec is String and mesh_spec.begins_with("res://") and ResourceLoader.exists(mesh_spec) and load(mesh_spec) is PackedScene:
			node = load(mesh_spec).instantiate()
		else:
			var mesh := _mesh(where + ".mesh", mesh_spec)
			if mesh == null:
				continue
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			var mat := _material(where, d)
			if mat:
				mi.material_override = mat
			node = mi
		node.name = d.get("name", "Model%d" % k)
		node.position = _vec3(where + ".position", d.get("position"))
		node.rotation_degrees.y = float(d.get("rotation_y", 0.0))
		var s = d.get("scale", 1.0)
		node.scale = Vector3(s, s, s) if (s is float or s is int) else _vec3(where + ".scale", s, Vector3.ONE)
		root.add_child(node, true)
		node.owner = root
		for c in node.find_children("*", "", true, false):
			c.owner = root if c.owner == null else c.owner
	if not models.is_empty():
		built["models"] = models.size()

	if spec.has("camera") and _check_keys("camera", "camera", spec.camera):
		var c: Dictionary = spec.camera
		var cam := Camera3D.new()
		cam.name = "Camera3D"
		root.add_child(cam)
		cam.owner = root
		cam.position = _vec3("camera.position", c.get("position"), Vector3(0, 6, 16))
		if c.has("lens"):
			if LENSES.has(c.lens):
				UZULook.configure_camera(cam, LENSES[c.lens])
			else:
				_err("camera.lens", "needs %s" % ", ".join(LENSES.keys()))
		if c.has("fov"):
			cam.fov = float(c.fov)
		var target := _vec3("camera.look_at", c.get("look_at"), Vector3.ZERO)
		if not cam.position.is_equal_approx(target):
			cam.transform = Transform3D(Basis(), cam.position).looking_at(target, Vector3.UP)
		built["camera"] = true

	# The look: sun, sky, fog, color; then the optional fill and rim lights.
	var quality: int = UZULook.QUALITY_STANDARD
	var atmosphere: int = UZULook.ATMOSPHERE_CLEAR
	if spec.has("look") and _check_keys("look", "look", spec.look):
		var l: Dictionary = spec.look
		if l.has("quality"):
			quality = QUALITIES.get(l.quality, -1)
			if quality < 0:
				_err("look.quality", "needs %s" % ", ".join(QUALITIES.keys()))
				quality = UZULook.QUALITY_STANDARD
		if l.has("atmosphere"):
			atmosphere = ATMOSPHERES.get(l.atmosphere, -1)
			if atmosphere < 0:
				_err("look.atmosphere", "needs %s" % ", ".join(ATMOSPHERES.keys()))
				atmosphere = UZULook.ATMOSPHERE_CLEAR
		built["look"] = UZULook.apply_to_scene(root, quality, atmosphere, bool(l.get("clouds", false)))
		if l.get("rig", false):
			for light_name in ["FillLight", "RimLight"]:  # Applying again replaces the rig, not doubles it.
				var old_light := root.get_node_or_null(light_name)
				if old_light:
					root.remove_child(old_light)
					old_light.free()
			UZULook.add_lighting_rig(root, atmosphere)
			built["rig"] = true

	var scatters := _list(spec, "scatter")
	var scatter_stats := []
	for k in scatters.size():
		var where := "scatter[%d]" % k
		var d = scatters[k]
		if not _check_keys(where, "scatter", d):
			continue
		var surface := root.get_node_or_null(str(d.get("on", "Ground"))) as MeshInstance3D
		if surface == null:
			_err(where + ".on", "no MeshInstance3D '%s' (add a ground or a model with that name)" % d.get("on", "Ground"))
			continue
		var node_name := str(d.get("name", "Scatter%d" % k))
		var old := root.get_node_or_null(node_name)
		if old:  # Applying again replaces the earlier props instead of stacking them.
			root.remove_child(old)
			old.free()
		var mesh := _mesh(where + ".mesh", d.get("mesh", "sphere"))
		if mesh == null:
			continue
		var mat := _material(where, d)
		if mat:
			mesh = mesh.duplicate()
			if mesh is PrimitiveMesh:
				mesh.material = mat
			else:
				mesh.surface_set_material(0, mat)
		var options := {}
		for key in ["density", "seed", "min_distance", "max_slope", "align", "sink", "max_count", "random_yaw"]:
			if d.has(key):
				options[key] = int(d[key]) if key in ["seed", "max_count"] else d[key]
		if d.has("scale"):
			options["scale_min"] = float(d.scale[0])
			options["scale_max"] = float(d.scale[1])
		var exclude := []
		for z in d.get("exclude", []):
			if z is Dictionary and z.has("center"):
				exclude.append({"center": _vec3(where + ".exclude.center", z.center), "radius": float(z.get("radius", 1.0))})
			elif z is Dictionary and z.has("aabb") and z.aabb.size() == 6:
				exclude.append({"aabb": AABB(Vector3(z.aabb[0], z.aabb[1], z.aabb[2]), Vector3(z.aabb[3], z.aabb[4], z.aabb[5]))})
			else:
				_err(where + ".exclude", "zones are {\"center\": [x, y, z], \"radius\": r} or {\"aabb\": [x, y, z, sx, sy, sz]}")
		if d.has("avoid"):
			exclude.append_array(_footprints(root, surface, float(d.avoid)))
		options["exclude"] = exclude
		var on := surface
		if d.has("near_camera"):
			on = _camera_patch(root, surface, float(d.near_camera))
			if on == null:
				_err(where + ".near_camera", "needs a Camera3D in the scene")
				continue
		var r := UZUScatter.scatter(root, on, mesh, options, node_name)
		if on != surface:
			root.remove_child(on)
			on.free()
		if not r.get("ok", false):
			_err(where, str(r.get("error", "scatter failed")))
			continue
		if not d.get("shadows", true):  # Tiny props (grass, flowers): shadows cost much and show little.
			var mmi := root.get_node(NodePath(r.node)) as GeometryInstance3D
			mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		scatter_stats.append({"node": r.node, "count": r.count, "target": r.target})
	if not scatter_stats.is_empty():
		built["scatter"] = scatter_stats

	var lamps := _list(spec, "lamps")
	for k in lamps.size():
		UZULook.add_lamp(root, _vec3("lamps[%d]" % k, lamps[k]), quality)
	if not lamps.is_empty():
		built["lamps"] = lamps.size()

	var particles := _list(spec, "particles")
	for k in particles.size():
		var where := "particles[%d]" % k
		var d = particles[k]
		if not _check_keys(where, "particles", d):
			continue
		if not PARTICLES.has(d.get("kind", "")):
			_err(where + ".kind", "needs %s" % ", ".join(PARTICLES.keys()))
			continue
		var p: GPUParticles3D = UZULook.create_particles(PARTICLES[d.kind], _vec3(where + ".extents", d.get("extents"), Vector3(4, 2, 4)))
		p.name = str(d.kind).capitalize()
		var old := root.get_node_or_null(NodePath(p.name))
		if old:
			root.remove_child(old)
			old.free()
		if str(d.get("position", "")) == "camera":
			# In front of the camera, a little above the ground (for presets, which do not know the scene).
			var cams := root.find_children("*", "Camera3D", true, false)
			var cam_t := _to_root(root, cams[0]) if not cams.is_empty() else Transform3D()
			var forward := -cam_t.basis.z
			forward.y = 0.0
			p.position = cam_t.origin + forward.normalized() * 10.0 if forward.length() > 0.01 else cam_t.origin
			p.position.y = 1.5
		else:
			p.position = _vec3(where + ".position", d.get("position"))
		root.add_child(p, true)
		p.owner = root
	if not particles.is_empty():
		built["particles"] = particles.size()

	var waters := _list(spec, "water")
	var names := {"shallow": "shallow_color", "deep": "deep_color", "clarity": "clarity", "waves": "wave_strength", "wave_scale": "wave_scale", "wave_speed": "wave_speed", "swell": "wave_height", "foam": "foam_width", "refraction": "refraction", "reflection": "reflection"}
	for k in waters.size():
		var where := "water[%d]" % k
		var d = waters[k]
		if not _check_keys(where, "water", d):
			continue
		var options := {}
		for key in names:
			if d.has(key):
				options[names[key]] = _color(where + "." + key, d[key]) if key in ["shallow", "deep"] else float(d[key])
		var size: Array = d.get("size", [10, 10])
		UZULook.add_water(root, _vec3(where + ".position", d.get("position")), Vector2(size[0], size[1]), options)
	if not waters.is_empty():
		built["water"] = waters.size()

	var rays := _list(spec, "godrays")
	for k in rays.size():
		var where := "godrays[%d]" % k
		var d = rays[k]
		if _check_keys(where, "godrays", d):
			UZULook.add_god_rays(root, _vec3(where + ".position", d.get("position")), float(d.get("width", 3.0)), float(d.get("length", 8.0)))
	if not rays.is_empty():
		built["godrays"] = rays.size()

	if not errors.is_empty():
		root.free()
		return {"ok": false, "error": "the spec has %d problem(s)" % errors.size(), "problems": errors}
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err == OK:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out.get_base_dir()))
		err = ResourceSaver.save(packed, out)
	root.free()
	if err != OK:
		return {"ok": false, "error": "could not save %s (%s)" % [out, error_string(err)]}
	if spec.get("main", false):
		ProjectSettings.set_setting("application/run/main_scene", out)
		ProjectSettings.save()
		built["main"] = true
	return {"ok": true, "scene": out, "built": built}
