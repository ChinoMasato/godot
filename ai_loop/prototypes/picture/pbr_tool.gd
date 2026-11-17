extends SceneTree
## Agent command line: one texture image → PBR maps (estimated, not scanned).
##   godot --headless --path P --script pbr_tool.gd -- make --in res://tex.png --out res://materials/stone [--kind stone]
##        [--strength 1.0] [--delight 0.7] [--tile] [--invert] [--size 1024]
##   godot --headless --path P --script pbr_tool.gd -- compare --normal a.png --ref b.png [--rough a_orm.png --ref-rough b_arm.png]
##   godot --headless --path P --script pbr_tool.gd -- kinds
## make writes albedo.png (baked light removed), normal.png (OpenGL: green = up, as Godot expects),
## orm.png (R = AO, G = roughness, B = metallic), height.png, and material.tres (ORMMaterial3D).
## Prints "UZU_RESULT {json}" with the files, statistics and warnings.

# Per kind (strengths fitted 2026-11-18 against Poly Haven scans: brick, cobblestone, rock): normal strength, roughness base and spread, metallic, height from dark (true = dark is high).
const KINDS := {
	"stone": {"strength": 0.55, "rough": 0.82, "spread": 0.18, "metal": 0.0, "detail": 0.5},
	"brick": {"strength": 0.65, "rough": 0.86, "spread": 0.14, "metal": 0.0, "detail": 0.4},
	"cobble": {"strength": 0.8, "rough": 0.62, "spread": 0.22, "metal": 0.0, "detail": 0.4},
	"ground": {"strength": 0.45, "rough": 0.9, "spread": 0.1, "metal": 0.0, "detail": 0.6},
	"wood": {"strength": 0.3, "rough": 0.7, "spread": 0.2, "metal": 0.0, "detail": 0.7},
	"plaster": {"strength": 0.25, "rough": 0.88, "spread": 0.08, "metal": 0.0, "detail": 0.6},
	"fabric": {"strength": 0.2, "rough": 0.95, "spread": 0.05, "metal": 0.0, "detail": 0.9},
	"leather": {"strength": 0.25, "rough": 0.6, "spread": 0.2, "metal": 0.0, "detail": 0.8},
	"metal": {"strength": 0.2, "rough": 0.4, "spread": 0.35, "metal": 1.0, "detail": 0.7},
	"painted_metal": {"strength": 0.2, "rough": 0.55, "spread": 0.3, "metal": 0.0, "detail": 0.7},
}


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var res := _run(args)
	print("UZU_RESULT " + JSON.stringify(res))
	quit(0 if res.get("ok", false) else 1)


func _opt(args: PackedStringArray, name: String, default: String = "") -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _load(path: String) -> Image:
	var img := Image.load_from_file(ProjectSettings.globalize_path(path))
	if img and img.is_compressed():
		img.decompress()
	return img


func _run(args: PackedStringArray) -> Dictionary:
	if args.is_empty():
		return {"ok": false, "error": "command needed: make | compare | kinds"}
	match args[0]:
		"kinds":
			return {"ok": true, "kinds": KINDS}
		"make":
			return _make(args)
		"compare":
			return _compare(args)
	return {"ok": false, "error": "unknown command"}


# ---------- float buffers (one channel) ----------

func _gray(img: Image) -> PackedFloat32Array:
	var w := img.get_width()
	var h := img.get_height()
	var out := PackedFloat32Array()
	out.resize(w * h)
	for y in h:
		for x in w:
			var c := img.get_pixel(x, y)
			out[y * w + x] = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
	return out


func _to_image(buf: PackedFloat32Array, w: int, h: int) -> Image:
	var bytes := buf.to_byte_array()
	return Image.create_from_data(w, h, false, Image.FORMAT_RF, bytes)


func _from_image(img: Image) -> PackedFloat32Array:
	return img.get_data().to_float32_array()


## Blur by shrinking and growing again (native and fast). factor = how many times smaller.
func _blur(buf: PackedFloat32Array, w: int, h: int, factor: int, tile: bool) -> PackedFloat32Array:
	var img := _to_image(buf, w, h)
	if tile:
		# Pad with wrapped copies so the blur also wraps around the edges.
		var big := Image.create(w * 3, h * 3, false, Image.FORMAT_RF)
		for j in 3:
			for i in 3:
				big.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(i * w, j * h))
		big.resize(maxi(3, w * 3 / factor), maxi(3, h * 3 / factor), Image.INTERPOLATE_BILINEAR)
		big.resize(w * 3, h * 3, Image.INTERPOLATE_CUBIC)
		img = big.get_region(Rect2i(w, h, w, h))
	else:
		img.resize(maxi(2, w / factor), maxi(2, h / factor), Image.INTERPOLATE_BILINEAR)
		img.resize(w, h, Image.INTERPOLATE_CUBIC)
	return _from_image(img)


func _percentile(buf: PackedFloat32Array, p: float) -> float:
	var s := buf.duplicate()
	s.sort()
	return s[clampi(int(s.size() * p), 0, s.size() - 1)]


# ---------- make ----------

func _make(args: PackedStringArray) -> Dictionary:
	var in_path := _opt(args, "in")
	var out_dir := _opt(args, "out")
	if in_path.is_empty() or out_dir.is_empty():
		return {"ok": false, "error": "make needs --in res://texture.png and --out res://materials/name"}
	var src := _load(in_path)
	if src == null:
		return {"ok": false, "error": "cannot read %s" % in_path}
	var kind_name := _opt(args, "kind", "stone")
	if not KINDS.has(kind_name):
		return {"ok": false, "error": "unknown --kind (see: kinds)"}
	var kind: Dictionary = KINDS[kind_name]
	var size := _opt(args, "size", "1024").to_int()
	src.convert(Image.FORMAT_RGBF)
	if src.get_width() > size:
		src.resize(size, int(round(size * float(src.get_height()) / src.get_width())), Image.INTERPOLATE_LANCZOS)
	var w := src.get_width()
	var h := src.get_height()
	var tile := args.has("--tile")
	var strength := _opt(args, "strength", "1.0").to_float() * float(kind.strength)
	var delight := clampf(_opt(args, "delight", "0.7").to_float(), 0.0, 1.0)
	var warnings := []

	var lum := _gray(src)
	var lit := _blur(lum, w, h, 64, tile)   # slow light across the image = baked shading
	var mean := 0.0
	for v in lum:
		mean += v
	mean /= lum.size()
	var light_range := _percentile(lit, 0.95) - _percentile(lit, 0.05)
	if light_range > 0.25:
		warnings.append("strong uneven lighting baked into the image (range %.2f); delight is removing it, check albedo.png" % light_range)

	# Albedo without the slow light gradient.
	var albedo := Image.create(w, h, false, Image.FORMAT_RGB8)
	for y in h:
		for x in w:
			var i := y * w + x
			var k := pow(mean / maxf(lit[i], 0.02), delight)
			var c := src.get_pixel(x, y) * clampf(k, 0.5, 2.0)
			albedo.set_pixel(x, y, Color(clampf(c.r, 0, 1), clampf(c.g, 0, 1), clampf(c.b, 0, 1)))

	# Height: detail above the slow light, a little smoothed; bright = high (mortar and cracks are dark).
	var detail: float = kind.detail
	var mid := _blur(lum, w, h, 6, tile)
	var height := PackedFloat32Array()
	height.resize(w * h)
	for i in w * h:
		# Two scales: shapes (lum - lit) and fine grain (lum - mid).
		height[i] = (lum[i] - lit[i]) * (1.0 - detail) + (lum[i] - mid[i]) * detail
	if args.has("--invert"):
		for i in w * h:
			height[i] = -height[i]
	height = _blur(height, w, h, 2, tile)
	var lo := _percentile(height, 0.02)
	var hi := _percentile(height, 0.98)
	for i in w * h:
		height[i] = clampf((height[i] - lo) / maxf(hi - lo, 1e-4), 0.0, 1.0)

	# Normal from height (central differences), OpenGL convention: +Y (green) is up in the image.
	var normal := Image.create(w, h, false, Image.FORMAT_RGB8)
	var scale := strength * float(w) / 512.0
	for y in h:
		for x in w:
			var xl := (x - 1 + w) % w if tile else maxi(x - 1, 0)
			var xr := (x + 1) % w if tile else mini(x + 1, w - 1)
			var yu := (y - 1 + h) % h if tile else maxi(y - 1, 0)
			var yd := (y + 1) % h if tile else mini(y + 1, h - 1)
			var dx := (height[y * w + xr] - height[y * w + xl]) * 0.5 * scale
			var dy := (height[yd * w + x] - height[yu * w + x]) * 0.5 * scale
			var n := Vector3(-dx, dy, 1.0).normalized()
			normal.set_pixel(x, y, Color(n.x * 0.5 + 0.5, n.y * 0.5 + 0.5, n.z * 0.5 + 0.5))
	# A normal map from an outside model (bring it through map_tool.gd import first, so its
	# direction, size and alignment are already checked) replaces the brightness-based one.
	var normal_source := "brightness"
	var normal_from := _opt(args, "normal-from")
	if not normal_from.is_empty():
		var given := _load(normal_from)
		if given == null:
			return {"ok": false, "error": "cannot read --normal-from %s" % normal_from}
		given.convert(Image.FORMAT_RGB8)
		given.resize(w, h, Image.INTERPOLATE_BILINEAR)
		normal = given
		normal_source = normal_from

	# AO: how far below its surroundings each point sits (kept gentle). Roughness: per kind, rougher in cavities.
	var around := _blur(height, w, h, 8, tile)
	var orm := Image.create(w, h, false, Image.FORMAT_RGB8)
	var rough_sum := 0.0
	for i in w * h:
		var cavity := maxf(0.0, around[i] - height[i])
		var ao := clampf(1.0 - cavity * 1.6, 0.35, 1.0)
		var r := clampf(float(kind.rough) + float(kind.spread) * (cavity * 2.0 - (height[i] - 0.5) * 0.6), 0.04, 1.0)
		rough_sum += r
		orm.set_pixel(i % w, i / w, Color(ao, r, kind.metal))

	var h_img := Image.create(w, h, false, Image.FORMAT_L8)
	for i in w * h:
		h_img.set_pixel(i % w, i / w, Color(height[i], height[i], height[i]))

	# Seam check: how different the left/right and top/bottom edges are (0 = tiles seamlessly).
	var seam := 0.0
	for y in h:
		seam += absf(lum[y * w] - lum[y * w + w - 1])
	for x in w:
		seam += absf(lum[x] - lum[(h - 1) * w + x])
	seam /= (w + h)
	if seam > 0.08 and not tile:
		warnings.append("edges do not match (seam %.2f): the texture will show seams when repeated" % seam)

	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out_dir))
	var files := {"albedo": out_dir + "/albedo.png", "normal": out_dir + "/normal.png", "orm": out_dir + "/orm.png", "height": out_dir + "/height.png"}
	albedo.save_png(files.albedo)
	normal.save_png(files.normal)
	orm.save_png(files.orm)
	h_img.save_png(files.height)

	# Material that uses the files at runtime (no import step needed).
	var mat := ORMMaterial3D.new()
	mat.albedo_texture = ImageTexture.create_from_image(albedo)
	mat.normal_enabled = true
	mat.normal_texture = ImageTexture.create_from_image(normal)
	mat.orm_texture = ImageTexture.create_from_image(orm)
	mat.heightmap_enabled = false
	files["material"] = out_dir + "/material.tres"
	ResourceSaver.save(mat, files.material)

	return {"ok": true, "command": "make", "kind": kind_name, "size": [w, h], "files": files, "normal_source": normal_source,
		"stats": {"roughness_mean": snappedf(rough_sum / (w * h), 0.01), "normal_strength": snappedf(strength, 0.01), "baked_light_range": snappedf(light_range, 0.01), "seam": snappedf(seam, 0.01)},
		"warnings": warnings, "note": "estimated maps (not a scan); check with the preview under a raking light"}


# ---------- compare (against reference maps, e.g. scanned ones) ----------

func _compare(args: PackedStringArray) -> Dictionary:
	var a := _load(_opt(args, "normal"))
	var b := _load(_opt(args, "ref"))
	if a == null or b == null:
		return {"ok": false, "error": "compare needs --normal and --ref"}
	var n := 256
	a.resize(n, n, Image.INTERPOLATE_BILINEAR)
	b.resize(n, n, Image.INTERPOLATE_BILINEAR)
	var ang := 0.0
	var ang_flat := 0.0
	for y in n:
		for x in n:
			var va := Vector3(a.get_pixel(x, y).r, a.get_pixel(x, y).g, a.get_pixel(x, y).b) * 2.0 - Vector3.ONE
			var vb := Vector3(b.get_pixel(x, y).r, b.get_pixel(x, y).g, b.get_pixel(x, y).b) * 2.0 - Vector3.ONE
			ang += rad_to_deg(va.normalized().angle_to(vb.normalized()))
			ang_flat += rad_to_deg(Vector3(0, 0, 1).angle_to(vb.normalized()))
	var res := {"ok": true, "normal_angle_error_deg": snappedf(ang / (n * n), 0.01), "flat_normal_error_deg": snappedf(ang_flat / (n * n), 0.01)}
	var ra := _opt(args, "rough")
	var rb := _opt(args, "ref-rough")
	if not ra.is_empty() and not rb.is_empty():
		var ia := _load(ra)
		var ib := _load(rb)
		ia.resize(n, n, Image.INTERPOLATE_BILINEAR)
		ib.resize(n, n, Image.INTERPOLATE_BILINEAR)
		var ga := PackedFloat32Array()
		var gb := PackedFloat32Array()
		for y in n:
			for x in n:
				ga.append(ia.get_pixel(x, y).g)
				gb.append(ib.get_pixel(x, y).g)
		var ma := 0.0
		var mb := 0.0
		for i in ga.size():
			ma += ga[i]
			mb += gb[i]
		ma /= ga.size()
		mb /= gb.size()
		var cov := 0.0
		var va := 0.0
		var vb := 0.0
		for i in ga.size():
			cov += (ga[i] - ma) * (gb[i] - mb)
			va += (ga[i] - ma) * (ga[i] - ma)
			vb += (gb[i] - mb) * (gb[i] - mb)
		res["roughness_mean"] = snappedf(ma, 0.01)
		res["ref_roughness_mean"] = snappedf(mb, 0.01)
		res["roughness_correlation"] = snappedf(cov / sqrt(maxf(va * vb, 1e-9)), 0.01)
	return res
