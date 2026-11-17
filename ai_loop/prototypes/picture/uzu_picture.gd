extends Node3D
## U.ZU Picture (prototype): turns one picture into a living 3D scene from a JSON spec.
## The picture becomes a relief mesh from its depth map, lit as a PBR surface (albedo = the picture,
## normal map, roughness from the "wet" region) by real Godot lights placed at picture pixels.
## Rain, embers and fog live in the same space, and a slow camera drift shows true parallax.
##
## Everything an agent sets is in the spec, in picture pixel coordinates (x right, y down),
## so an agent can look at the picture, name points and regions, render, look again and adjust.
## See picture_tool.gd (analyze / validate) and README.md for the spec.

@export_file("*.json") var spec_path := "res://picture.json"

const REGION_A := ["glow", "wet", "sky", "flicker"]
const REGION_B := ["waterfall", "sway", "water"]

var spec := {}
var img_size := Vector2i.ZERO
var tan_half := 0.0
var aspect := 1.0
var _depth: Image
var _da := 0.0
var _db := 0.0
var _kind := "disparity"
var _lights: Array[Dictionary] = []
var _camera: Camera3D
var _t := 0.0
var _relief_mats: Array[ShaderMaterial] = []
var _fog_mats: Array[ShaderMaterial] = []
var _bolt: DirectionalLight3D
var _flash_at := -1.0      # time of the next lightning strike
var _flash_rng := RandomNumberGenerator.new()
var stats := {}            # filled while building; printed for agents (UZU_PICTURE line)
var _noise: ImageTexture   # seamless noise shared by the shaders (clouds, fog, ripples)


static func load_spec(path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	var parsed = JSON.parse_string(text)
	return parsed if parsed is Dictionary else {}


static func load_image(path: String) -> Image:
	var img := Image.load_from_file(ProjectSettings.globalize_path(path))
	if img and img.is_compressed():
		img.decompress()
	return img


func _ready() -> void:
	spec = load_spec(spec_path)
	if spec.is_empty():
		push_error("U.ZU Picture: could not read spec %s" % spec_path)
		return
	_build()


## Image pixel (x right, y down) and depth value 0..1 → point in front of the camera (camera at origin, looking -Z).
func place(px: Vector2, d: float) -> Vector3:
	var z := 1.0 / (_da + _db * d) if _kind == "disparity" else lerpf(spec.depth.get("near", 4.5), spec.depth.get("far", 160.0), d)
	var u := px.x / img_size.x
	var v := px.y / img_size.y
	return Vector3((u * 2.0 - 1.0) * tan_half * aspect * z, (1.0 - v * 2.0) * tan_half * z, -z)


## Depth value (0..1, bilinear) at an image pixel.
func depth_at(px: Vector2) -> float:
	var x := clampf(px.x / img_size.x, 0, 1) * (_depth.get_width() - 1)
	var y := clampf(px.y / img_size.y, 0, 1) * (_depth.get_height() - 1)
	var x0 := int(x)
	var y0 := int(y)
	var x1 := mini(x0 + 1, _depth.get_width() - 1)
	var y1 := mini(y0 + 1, _depth.get_height() - 1)
	var fx := x - x0
	var fy := y - y0
	var a := lerpf(_depth.get_pixel(x0, y0).r, _depth.get_pixel(x1, y0).r, fx)
	var b := lerpf(_depth.get_pixel(x0, y1).r, _depth.get_pixel(x1, y1).r, fx)
	return lerpf(a, b, fy)


func point_at(px: Vector2) -> Vector3:
	return place(px, depth_at(px))


func _build() -> void:
	var art := load_image(spec.image)
	img_size = art.get_size()
	aspect = float(img_size.x) / img_size.y
	var fov: float = spec.get("fov", 38.0)
	tan_half = tan(deg_to_rad(fov) * 0.5)
	var depth_spec: Dictionary = spec.depth
	_depth = load_image(depth_spec.path)
	_depth.convert(Image.FORMAT_RF)
	_kind = depth_spec.get("kind", "disparity")
	var near: float = depth_spec.get("near", 4.5)
	var far: float = depth_spec.get("far", 160.0)
	_da = 1.0 / far
	_db = 1.0 / near - 1.0 / far

	var quality: String = spec.get("look", {}).get("quality", "high")
	var fn := FastNoiseLite.new()
	fn.noise_type = FastNoiseLite.TYPE_PERLIN
	fn.frequency = 0.02
	fn.fractal_type = FastNoiseLite.FRACTAL_NONE
	var nimg := fn.get_seamless_image(256, 256)
	nimg.convert(Image.FORMAT_RGBA8)
	nimg.generate_mipmaps()
	_noise = ImageTexture.create_from_image(nimg)
	_build_relief(art, quality)
	_build_lights()
	_build_effects(quality)
	_build_camera(fov)
	_build_look()
	stats["fog_sheets"] = _fog_mats.size()
	stats["lightning"] = _bolt != null
	print("UZU_PICTURE " + JSON.stringify(stats))


## Each region is a mask image plus a channel; the shader reads it as tex * channel selector.
func _set_regions(mat: ShaderMaterial) -> void:
	var regions: Dictionary = spec.get("regions", {})
	var cache := {}
	for name in REGION_A + REGION_B:
		var sel := Vector4.ZERO
		var tex: Texture2D = null
		if regions.has(name):
			var r: Dictionary = regions[name]
			if not cache.has(r.path):
				var src := load_image(r.path)
				src.convert(Image.FORMAT_RGBA8)
				cache[r.path] = ImageTexture.create_from_image(src)
			tex = cache[r.path]
			sel["rgba".find(r.get("channel", "r"))] = 1.0
		mat.set_shader_parameter("r_" + name, tex)
		mat.set_shader_parameter("c_" + name, sel)


func _build_relief(art: Image, quality: String) -> void:
	var sx := 220 if quality == "low" else 360
	var sy := int(round(sx / aspect))
	var relief: Dictionary = spec.get("relief", {})
	# Disparity on the grid.
	var d := PackedFloat32Array()
	d.resize((sx + 1) * (sy + 1))
	for j in sy + 1:
		for i in sx + 1:
			d[j * (sx + 1) + i] = depth_at(Vector2(float(i) / sx, float(j) / sy) * Vector2(img_size))
	# Front layer: torn where depth jumps (outlines of near things), so near and far slide apart as
	# separate layers instead of stretching like a rubber sheet.
	var tear: float = relief.get("tear", 0.05)
	var front := _grid_mesh(d, sx, sy, tear, 1.0)
	# Backdrop: the same picture where near things are pushed back to the depth around them
	# (smallest disparity nearby), so the gaps behind torn edges show background, not holes.
	var back_d := d
	var radius: int = relief.get("backdrop_radius", 6)
	if relief.get("backdrop", true):
		back_d = _min_filter(d, sx + 1, sy + 1, radius)
	var backdrop := _grid_mesh(back_d, sx, sy, 0.0, 1.03) if relief.get("backdrop", true) else null

	var mat := ShaderMaterial.new()
	mat.shader = load("res://uzu_picture.gdshader")
	mat.set_shader_parameter("art", ImageTexture.create_from_image(art))
	_set_regions(mat)
	mat.set_shader_parameter("noise_tex", _noise)
	if spec.has("normal"):
		mat.set_shader_parameter("normal_tex", ImageTexture.create_from_image(load_image(spec.normal)))
		mat.set_shader_parameter("has_normal", true)
	_set_clouds(mat)
	_relief_mats.append(mat)
	var mi := MeshInstance3D.new()
	mi.name = "Relief"
	mi.mesh = front
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	if backdrop:
		var bm: ShaderMaterial = mat.duplicate()
		bm.set_shader_parameter("backdrop", true)
		# The backdrop must not carry its own copy of the near things (it would show as a ghost
		# beside them when the camera moves): paint them out and fill from the surroundings.
		var t0 := Time.get_ticks_msec()
		bm.set_shader_parameter("art", ImageTexture.create_from_image(_fill_behind(art, d, back_d, sx, sy)))
		stats["backdrop_fill_ms"] = Time.get_ticks_msec() - t0
		_relief_mats.append(bm)
		var bi := MeshInstance3D.new()
		bi.name = "Backdrop"
		bi.mesh = backdrop
		bi.material_override = bm
		bi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(bi)


## Grid mesh over the picture from grid disparities. tear > 0 drops triangles whose far/near
## distance ratio is above it. push > 1 moves every point farther away.
func _grid_mesh(d: PackedFloat32Array, sx: int, sy: int, tear: float, push: float) -> ArrayMesh:
	var verts := PackedVector3Array()
	var uvs := PackedVector2Array()
	var zs := PackedFloat32Array()
	verts.resize((sx + 1) * (sy + 1))
	uvs.resize(verts.size())
	zs.resize(verts.size())
	for j in sy + 1:
		for i in sx + 1:
			var k := j * (sx + 1) + i
			var uv := Vector2(float(i) / sx, float(j) / sy)
			var p := place(uv * Vector2(img_size), d[k]) * push
			verts[k] = p
			uvs[k] = uv
			zs[k] = -p.z
	var idx := PackedInt32Array()
	var torn := 0
	for j in sy:
		for i in sx:
			var a := j * (sx + 1) + i
			for tri in [[a, a + sx + 1, a + 1], [a + 1, a + sx + 1, a + sx + 2]]:
				if tear > 0.0:
					# Parallax on screen follows the disparity difference (not the distance ratio,
					# which is huge but harmless in the far sky), so cut where disparity jumps.
					var dmin := minf(d[tri[0]], minf(d[tri[1]], d[tri[2]]))
					var dmax := maxf(d[tri[0]], maxf(d[tri[1]], d[tri[2]]))
					if dmax - dmin > tear:
						torn += 1
						continue
				idx.append_array(tri)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	var normals := PackedVector3Array()
	normals.resize(verts.size())
	normals.fill(Vector3(0, 0, 1))
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	if tear > 0.0:
		stats["torn_triangles"] = torn
	return mesh


## The picture with near things removed: where the depth is clearly nearer than the backdrop depth,
## the color is filled from around by pull-push (average down a pyramid, fill holes on the way up).
func _fill_behind(art: Image, d: PackedFloat32Array, back_d: PackedFloat32Array, sx: int, sy: int) -> Image:
	var w := art.get_width() / 2
	var h := art.get_height() / 2
	var mask := Image.create(sx + 1, sy + 1, false, Image.FORMAT_RF)
	for j in sy + 1:
		for i in sx + 1:
			var k := j * (sx + 1) + i
			mask.set_pixel(i, j, Color(1.0 if d[k] - back_d[k] > 0.04 else 0.0, 0, 0))
	mask.resize(w, h, Image.INTERPOLATE_BILINEAR)
	var base: Image = art.duplicate()
	base.resize(w, h, Image.INTERPOLATE_BILINEAR)
	base.convert(Image.FORMAT_RGBAF)
	var filled := 0
	for y in h:
		for x in w:
			var wgt := 1.0 if mask.get_pixel(x, y).r < 0.1 else 0.0  # grow the hole a little
			var c := base.get_pixel(x, y)
			base.set_pixel(x, y, Color(c.r * wgt, c.g * wgt, c.b * wgt, wgt))
			filled += int(wgt == 0.0)
	stats["backdrop_filled_share"] = snappedf(float(filled) / (w * h), 0.001)
	var levels: Array[Image] = [base]
	while levels[-1].get_width() > 4 and levels[-1].get_height() > 4:
		var nxt: Image = levels[-1].duplicate()
		nxt.resize(maxi(1, nxt.get_width() / 2), maxi(1, nxt.get_height() / 2), Image.INTERPOLATE_BILINEAR)
		levels.append(nxt)
	var up: Image = levels[-1]
	for li in range(levels.size() - 2, -1, -1):
		var lvl: Image = levels[li]
		var coarse: Image = up.duplicate()
		coarse.resize(lvl.get_width(), lvl.get_height(), Image.INTERPOLATE_BILINEAR)
		for y in lvl.get_height():
			for x in lvl.get_width():
				var f := lvl.get_pixel(x, y)
				var c := coarse.get_pixel(x, y)
				var ca := maxf(c.a, 1e-4)
				# premultiplied: fine + (1 - fine alpha) * normalized coarse
				var r := f.r + (1.0 - f.a) * c.r / ca
				var g := f.g + (1.0 - f.a) * c.g / ca
				var b := f.b + (1.0 - f.a) * c.b / ca
				lvl.set_pixel(x, y, Color(r, g, b, 1.0))
		up = lvl
	var out: Image = levels[0]
	out.convert(Image.FORMAT_RGB8)
	return out


## Smallest value in a square around each cell (separable), = the farthest depth nearby.
func _min_filter(src: PackedFloat32Array, w: int, h: int, r: int) -> PackedFloat32Array:
	var tmp := PackedFloat32Array()
	tmp.resize(src.size())
	for y in h:
		for x in w:
			var m := 1.0
			for dx in range(-r, r + 1):
				m = minf(m, src[y * w + clampi(x + dx, 0, w - 1)])
			tmp[y * w + x] = m
	var out := PackedFloat32Array()
	out.resize(src.size())
	for y in h:
		for x in w:
			var m := 1.0
			for dy in range(-r, r + 1):
				m = minf(m, tmp[clampi(y + dy, 0, h - 1) * w + x])
			out[y * w + x] = m
	return out


func _set_clouds(mat: ShaderMaterial) -> void:
	var c: Dictionary = spec.get("effects", {}).get("clouds", {})
	mat.set_shader_parameter("cloud_amount", float(c.get("amount", 0.0)))
	mat.set_shader_parameter("cloud_speed", float(c.get("speed", 1.0)))
	var dir: Array = c.get("direction", [1, 0])
	mat.set_shader_parameter("cloud_dir", Vector2(dir[0], -dir[1]).normalized())


func _build_lights() -> void:
	var i := 0
	for l: Dictionary in spec.get("lights", []):
		var at := Vector2(l.at[0], l.at[1])
		var p := point_at(at) * 0.93  # just in front of the surface
		var light := OmniLight3D.new()
		light.name = "Light%d" % i
		light.position = p
		light.light_color = Color.html(l.get("color", "#ffb060"))
		var energy: float = l.get("energy", 1.0)
		light.light_energy = energy * 1.5
		# Range grows with distance so far lamps still light their surroundings.
		light.omni_range = maxf(1.5, -p.z * 0.35) * l.get("reach", 1.0)
		light.omni_attenuation = 1.6
		light.shadow_enabled = false
		add_child(light)
		_lights.append({"node": light, "energy": light.light_energy, "flicker": float(l.get("flicker", 0.0)), "seed": i * 13.0})
		i += 1
	if spec.has("moon"):
		var m: Dictionary = spec.moon
		var dir := place(Vector2(m.at[0], m.at[1]), 0.0).normalized()
		var moon := DirectionalLight3D.new()
		moon.name = "Moon"
		moon.light_color = Color.html(m.get("color", "#a8bce0"))
		moon.light_energy = m.get("energy", 0.6)
		moon.shadow_enabled = false
		add_child(moon)
		moon.look_at_from_position(Vector3.ZERO, -dir, Vector3.UP)


func _ember_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	m.vertex_color_use_as_albedo = true
	m.albedo_color = Color(6.0, 2.2, 0.5)
	var g := Gradient.new()
	g.set_color(0, Color(1, 1, 1, 1))
	g.set_color(1, Color(1, 1, 1, 0))
	var tex := GradientTexture2D.new()
	tex.gradient = g
	tex.fill = GradientTexture2D.FILL_RADIAL
	tex.fill_from = Vector2(0.5, 0.5)
	tex.fill_to = Vector2(1.0, 0.5)
	m.albedo_texture = tex
	return m


func _build_effects(quality: String) -> void:
	var fx: Dictionary = spec.get("effects", {})
	var low := quality == "low"
	if fx.has("rain"):
		var rain := GPUParticles3D.new()
		rain.name = "Rain"
		rain.amount = int(fx.rain.get("amount", 1600) * (0.45 if low else 1.0))
		rain.lifetime = 1.6
		rain.preprocess = 2.0
		rain.visibility_aabb = AABB(Vector3(-20, -12, -40), Vector3(40, 30, 42))
		var pm := ParticleProcessMaterial.new()
		pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
		pm.emission_box_extents = Vector3(13, 1, 15)
		pm.direction = Vector3(0.12, -1, 0)
		pm.spread = 3.0
		pm.initial_velocity_min = 9.0
		pm.initial_velocity_max = 14.0
		pm.gravity = Vector3.ZERO
		pm.particle_flag_align_y = true
		rain.process_material = pm
		rain.position = Vector3(0, 9, -18)
		var q := QuadMesh.new()
		q.size = Vector2(0.012, 0.45)
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
		m.albedo_color = Color(0.72, 0.8, 0.88, fx.rain.get("opacity", 0.14))
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
		q.material = m
		rain.draw_pass_1 = q
		add_child(rain)
	if fx.has("fog") and fx.fog.has("layers"):
		# Sheets of mist at several distances, each drifting at its own speed: as the camera moves,
		# they slide past each other and past the picture, which reads as deep space.
		var k := 0
		for z in fx.fog.layers:
			var zf := float(z)
			var q := QuadMesh.new()
			q.size = Vector2(2.0 * tan_half * aspect * zf * 1.35, 2.0 * tan_half * zf * 1.35)
			var m := ShaderMaterial.new()
			m.shader = load("res://uzu_fog_sheet.gdshader")
			m.set_shader_parameter("amount", float(fx.fog.get("amount", 0.45)))
			m.set_shader_parameter("speed", 0.05 / sqrt(zf) * float(fx.fog.get("speed", 1.0)))
			m.set_shader_parameter("seed", k * 7.3)
			m.set_shader_parameter("top", float(fx.fog.get("top", 0.1)))
			m.set_shader_parameter("color", Color.html(fx.fog.get("color", "#222a33")))
			m.set_shader_parameter("noise_tex", _noise)
			_fog_mats.append(m)
			var mi := MeshInstance3D.new()
			mi.name = "Fog%d" % k
			mi.mesh = q
			mi.material_override = m
			mi.position = Vector3(0, 0, -zf)
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			add_child(mi)
			k += 1
	if fx.has("lightning"):
		var lt: Dictionary = fx.lightning
		_flash_rng.seed = int(lt.get("seed", 1))
		_flash_at = float(lt.get("first", 2.0))
		_bolt = DirectionalLight3D.new()
		_bolt.name = "Lightning"
		_bolt.light_color = Color(0.75, 0.82, 1.0)
		_bolt.light_energy = 0.0
		_bolt.shadow_enabled = false
		add_child(_bolt)
		var at: Array = lt.get("at", spec.get("moon", {}).get("at", [img_size.x * 0.5, 0]))
		var dir := place(Vector2(at[0], at[1]), 0.0).normalized()
		_bolt.look_at_from_position(Vector3.ZERO, -dir + Vector3(0, -0.3, 0), Vector3.UP)
	if fx.has("embers"):
		var per: int = maxi(4, int(fx.embers.get("amount", 48) * (0.4 if low else 1.0)) / maxi(1, fx.embers.from.size()))
		var k := 0
		for at in fx.embers.from:
			var p := point_at(Vector2(at[0], at[1])) * 0.95
			var e := GPUParticles3D.new()
			e.name = "Embers%d" % k
			e.amount = per
			e.lifetime = 3.0
			e.preprocess = 3.0
			e.position = p
			var pm := ParticleProcessMaterial.new()
			pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
			pm.emission_sphere_radius = 0.02 * -p.z
			pm.direction = Vector3(0, 1, 0)
			pm.spread = 25.0
			pm.initial_velocity_min = 0.02 * -p.z
			pm.initial_velocity_max = 0.06 * -p.z
			pm.gravity = Vector3(0.01, 0.01, 0) * -p.z
			pm.turbulence_enabled = true
			pm.turbulence_noise_strength = 0.6
			pm.scale_min = 0.6
			pm.scale_max = 1.2
			var fade := Gradient.new()
			fade.set_color(0, Color(1, 1, 1, 1))
			fade.set_color(1, Color(1, 0.4, 0.1, 0))
			var ft := GradientTexture1D.new()
			ft.gradient = fade
			pm.color_ramp = ft
			e.process_material = pm
			var q := QuadMesh.new()
			q.size = Vector2.ONE * 0.004 * -p.z
			q.material = _ember_material()
			e.draw_pass_1 = q
			e.visibility_aabb = AABB(Vector3(-5, -2, -5), Vector3(10, 10, 10))
			add_child(e)
			k += 1


func _build_camera(fov: float) -> void:
	_camera = Camera3D.new()
	_camera.name = "Camera"
	# Show a little less than the picture, so its edges stay out of view while the camera drifts.
	var overscan: float = spec.get("camera", {}).get("overscan", 0.07)
	_camera.fov = rad_to_deg(2.0 * atan(tan_half * (1.0 - overscan)))
	_camera.near = 0.05
	_camera.far = 400.0
	add_child(_camera)
	_camera.make_current()


func _build_look() -> void:
	var look: Dictionary = spec.get("look", {})
	var qualities := {"high": UZULook.QUALITY_HIGH, "standard": UZULook.QUALITY_STANDARD, "low": UZULook.QUALITY_LOW}
	var atmospheres := {"clear": UZULook.ATMOSPHERE_CLEAR, "humid": UZULook.ATMOSPHERE_HUMID, "golden_hour": UZULook.ATMOSPHERE_GOLDEN_HOUR, "night": UZULook.ATMOSPHERE_NIGHT}
	var env := UZULook.create_environment(qualities.get(look.get("quality", "high"), 0), atmospheres.get(look.get("atmosphere", "night"), 3), false)
	# The picture already carries its own light and air: keep the engine's additions gentle.
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.025, 0.04)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.0, 0.0, 0.0)
	env.ambient_light_energy = 0.0
	env.ssao_enabled = false
	env.ssil_enabled = false
	env.ssr_enabled = false
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.tonemap_exposure = 1.0
	env.adjustment_enabled = false
	env.fog_density *= float(spec.get("effects", {}).get("fog", {}).get("density", 0.0))
	env.glow_enabled = true
	env.glow_hdr_threshold = 1.0
	env.glow_intensity = look.get("bloom", 0.8)
	var we := WorldEnvironment.new()
	we.name = "Look"
	we.environment = env
	if look.get("post", true):
		var comp := Compositor.new()
		var post := UZUPostEffect.new()
		post.edge_strength = look.get("edge", 0.15)
		post.vignette_strength = look.get("vignette", 0.4)
		comp.compositor_effects = [post]
		we.compositor = comp
	add_child(we)


## Lightning: a strike is two quick pulses and a fading glow. Strikes come every "every" seconds
## (random within the range, fixed by "seed"), the first at "first", so a shot can catch one.
func _update_lightning() -> void:
	if _flash_at < 0.0:
		return
	var lt: Dictionary = spec.effects.lightning
	var s := _t - _flash_at
	var f := 0.0
	if s >= 0.0:
		f = maxf(exp(-s * 30.0), 0.75 * exp(-maxf(s - 0.16, 0.0) * 9.0) * float(s > 0.16)) + 0.25 * exp(-s * 2.5)
		if s > 2.0:
			var every: Array = lt.get("every", [6, 14])
			_flash_at = _t + _flash_rng.randf_range(every[0], every[1])
	f *= float(lt.get("strength", 1.0))
	for m in _relief_mats:
		m.set_shader_parameter("flash", f)
	for m in _fog_mats:
		m.set_shader_parameter("flash", f)
	if _bolt:
		_bolt.light_energy = f * 3.0


func _process(delta: float) -> void:
	_t += delta
	var cam: Dictionary = spec.get("camera", {})
	var drift: float = cam.get("drift", 1.0)
	var push_k: float = cam.get("push", 1.0)
	var sw := sin(_t * TAU / 23.0)
	var push := 0.5 - 0.5 * cos(_t * TAU / 31.0)
	if _camera:
		_camera.position = Vector3(sw * 0.2 * drift, sin(_t * TAU / 19.0) * 0.05 * drift, -0.05 - push * 0.32 * push_k)
		_camera.look_at(Vector3(_camera.position.x * 0.25, _camera.position.y * 0.25, -26.0))
	_update_lightning()
	for l in _lights:
		var f: float = l.flicker
		if f > 0.0:
			var n := sin(_t * 7.1 + l.seed) * 0.5 + sin(_t * 19.3 + l.seed * 2.1) * 0.25 + sin(_t * 31.7 + l.seed * 0.7) * 0.15
			(l.node as OmniLight3D).light_energy = l.energy * (1.0 + f * 0.35 * n)
