extends SceneTree
## Bring in maps made outside the engine (by any AI model or tool) and make them safe to use.
## The engine does not estimate shape itself; agents run their own estimator and hand the result here.
##
##   import  --kind normal|depth --in res://model_output.png --for res://picture.png --out res://normal.png
##           [--depth-kind auto|disparity|linear] [--no-align]
##     Checks and fixes: size and aspect (resized to the picture), normal map green direction
##     (OpenGL vs DirectX, judged from the picture's own shading), vector length, depth direction
##     (white = near or far, judged from bottom vs top), and a small offset against the picture.
##     Prints what it found and what it changed, so an agent can trust or override it.
##   estimate --kind normal|depth --in res://picture.png --out res://x.png [--config res://estimators.json]
##     Runs the command configured for that kind (e.g. a Depth Anything script), then import.
##     estimators.json: {"depth": "python my_depth.py {in} {out}", "normal": "..."}  ({in}/{out} are absolute paths)
##
## Prints "UZU_RESULT {json}".

const N := 256  # analysis size


func _initialize() -> void:
	var res := _run(OS.get_cmdline_user_args())
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
		return {"ok": false, "error": "command needed: import | estimate"}
	match args[0]:
		"import":
			return _import(args)
		"estimate":
			return _estimate(args)
	return {"ok": false, "error": "unknown command"}


func _estimate(args: PackedStringArray) -> Dictionary:
	var kind := _opt(args, "kind")
	var conf_path := _opt(args, "config", "res://estimators.json")
	var conf = JSON.parse_string(FileAccess.get_file_as_string(conf_path))
	if not (conf is Dictionary) or not conf.has(kind):
		return {"ok": false, "error": "no estimator for '%s' in %s; add e.g. {\"%s\": \"python my_model.py {in} {out}\"}" % [kind, conf_path, kind]}
	var src := ProjectSettings.globalize_path(_opt(args, "in"))
	var raw := ProjectSettings.globalize_path(_opt(args, "out")).get_basename() + "_raw.png"
	var cmd: String = conf[kind].replace("{in}", "\"%s\"" % src).replace("{out}", "\"%s\"" % raw)
	var output := []
	var code := OS.execute("cmd.exe" if OS.get_name() == "Windows" else "sh", ["/c", cmd] if OS.get_name() == "Windows" else ["-c", cmd], output, true)
	if code != 0 or not FileAccess.file_exists(raw):
		return {"ok": false, "error": "estimator failed (exit %d)" % code, "command": cmd, "log": "".join(output).right(1500)}
	var a := PackedStringArray(["import", "--kind", kind, "--in", raw, "--for", _opt(args, "in"), "--out", _opt(args, "out")])
	var res := _import(a)
	res["estimator"] = cmd
	return res


# ---------- helpers on small float grids ----------

func _lum_grid(img: Image, w: int, h: int) -> PackedFloat32Array:
	var s: Image = img.duplicate()
	s.convert(Image.FORMAT_RGBF)
	s.resize(w, h, Image.INTERPOLATE_BILINEAR)
	var out := PackedFloat32Array()
	out.resize(w * h)
	for y in h:
		for x in w:
			var c := s.get_pixel(x, y)
			out[y * w + x] = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
	return out


func _chan_grid(img: Image, w: int, h: int, ch: int) -> PackedFloat32Array:
	var s: Image = img.duplicate()
	s.convert(Image.FORMAT_RGBAF)
	s.resize(w, h, Image.INTERPOLATE_BILINEAR)
	var out := PackedFloat32Array()
	out.resize(w * h)
	for y in h:
		for x in w:
			out[y * w + x] = s.get_pixel(x, y)[ch]
	return out


func _corr(a: PackedFloat32Array, b: PackedFloat32Array) -> float:
	var ma := 0.0
	var mb := 0.0
	for i in a.size():
		ma += a[i]
		mb += b[i]
	ma /= a.size()
	mb /= b.size()
	var c := 0.0
	var va := 0.0
	var vb := 0.0
	for i in a.size():
		c += (a[i] - ma) * (b[i] - mb)
		va += (a[i] - ma) * (a[i] - ma)
		vb += (b[i] - mb) * (b[i] - mb)
	return c / sqrt(maxf(va * vb, 1e-12))


## Gradient magnitude of a grid (edges), for alignment.
func _edges(g: PackedFloat32Array, w: int, h: int) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(w * h)
	for y in range(1, h - 1):
		for x in range(1, w - 1):
			var dx := g[y * w + x + 1] - g[y * w + x - 1]
			var dy := g[(y + 1) * w + x] - g[(y - 1) * w + x]
			out[y * w + x] = sqrt(dx * dx + dy * dy)
	return out


## Best (dx, dy) in grid cells that moves b onto a, searching +-r; returns [dx, dy, score, score_at_zero].
func _best_shift(a: PackedFloat32Array, b: PackedFloat32Array, w: int, h: int, r: int) -> Array:
	var best := [0, 0, -2.0, 0.0]
	for dy in range(-r, r + 1):
		for dx in range(-r, r + 1):
			var s := 0.0
			var n := 0
			for y in range(r, h - r, 2):
				for x in range(r, w - r, 2):
					s += a[y * w + x] * b[(y + dy) * w + x + dx]
					n += 1
			s /= maxf(n, 1)
			if dx == 0 and dy == 0:
				best[3] = s
			if s > best[2]:
				best = [dx, dy, s, best[3]]
	return best


# ---------- import ----------

func _import(args: PackedStringArray) -> Dictionary:
	var kind := _opt(args, "kind")
	var src := _load(_opt(args, "in"))
	var pic := _load(_opt(args, "for"))
	var out_path := _opt(args, "out")
	if src == null or pic == null or out_path.is_empty() or not kind in ["normal", "depth"]:
		return {"ok": false, "error": "import needs --kind normal|depth, --in, --for (the picture) and --out"}
	var found := {}
	var changed := []
	var warnings := []
	var pw := pic.get_width()
	var ph := pic.get_height()
	var aspect_in := float(src.get_width()) / src.get_height()
	var aspect_pic := float(pw) / ph
	found["size_in"] = [src.get_width(), src.get_height()]
	if absf(aspect_in / aspect_pic - 1.0) > 0.01:
		warnings.append("aspect differs (map %.3f, picture %.3f): the model may have cropped or padded; check alignment" % [aspect_in, aspect_pic])
	var w := N
	var h := maxi(16, int(round(N / aspect_pic)))
	var lum := _lum_grid(pic, w, h)

	# Offset against the picture (models sometimes shift by a few pixels after resizing).
	var map_signal: PackedFloat32Array
	if kind == "depth":
		map_signal = _chan_grid(src, w, h, 0)
	else:
		var r := _chan_grid(src, w, h, 0)
		var g := _chan_grid(src, w, h, 1)
		map_signal = PackedFloat32Array()
		map_signal.resize(w * h)
		for i in w * h:
			map_signal[i] = r[i] + g[i]
	var shift := [0, 0]
	if not args.has("--no-align"):
		var bs := _best_shift(_edges(lum, w, h), _edges(map_signal, w, h), w, h, 6)
		found["offset_px"] = [roundi(bs[0] * float(pw) / w), roundi(bs[1] * float(ph) / h)]
		found["offset_confidence"] = snappedf((bs[2] - bs[3]) / maxf(absf(bs[3]), 1e-6), 0.01)
		if (bs[0] != 0 or bs[1] != 0) and bs[2] > bs[3] * 1.05:
			shift = [bs[0], bs[1]]

	var img: Image = src.duplicate()
	img.convert(Image.FORMAT_RGBAF)
	img.resize(pw, ph, Image.INTERPOLATE_BILINEAR)
	if img.get_size() != src.get_size():
		changed.append("resized %dx%d -> %dx%d" % [src.get_width(), src.get_height(), pw, ph])
	if shift != [0, 0]:
		var ox := roundi(shift[0] * float(pw) / w)
		var oy := roundi(shift[1] * float(ph) / h)
		var moved := Image.create(pw, ph, false, Image.FORMAT_RGBAF)
		for y in ph:
			for x in pw:
				moved.set_pixel(x, y, img.get_pixel(clampi(x + ox, 0, pw - 1), clampi(y + oy, 0, ph - 1)))
		img = moved
		changed.append("moved by %d, %d px to line up with the picture" % [-ox, -oy])

	if kind == "normal":
		_fix_normal(img, lum, w, h, found, changed, warnings)
	else:
		_fix_depth(img, w, h, found, changed, warnings, _opt(args, "depth-kind", "auto"))

	var out := img
	out.convert(Image.FORMAT_RGB8 if kind == "normal" else Image.FORMAT_L8)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out_path.get_base_dir()))
	out.save_png(out_path)
	return {"ok": true, "command": "import", "kind": kind, "out": out_path, "found": found, "changed": changed, "warnings": warnings}


func _fix_normal(img: Image, lum: PackedFloat32Array, w: int, h: int, found: Dictionary, changed: Array, warnings: Array) -> void:
	# The picture's own shading: brightness slopes. In an OpenGL normal map, R follows -d(height)/dx
	# and G follows +d(height)/dy (image rows go down). Whether bright means high or low differs per
	# picture, so judge G relative to R: same relation = OpenGL, opposite = DirectX (green flipped).
	var r := _chan_grid(img, w, h, 0)
	var g := _chan_grid(img, w, h, 1)
	var b := _chan_grid(img, w, h, 2)
	var gx := PackedFloat32Array()
	var gy := PackedFloat32Array()
	var rr := PackedFloat32Array()
	var gg := PackedFloat32Array()
	var len_sum := 0.0
	var z_sum := 0.0
	for y in range(1, h - 1):
		for x in range(1, w - 1):
			var i := y * w + x
			gx.append(-(lum[i + 1] - lum[i - 1]))
			gy.append(lum[i + w] - lum[i - w])
			rr.append(r[i])
			gg.append(g[i])
			var v := Vector3(r[i], g[i], b[i]) * 2.0 - Vector3.ONE
			len_sum += v.length()
			z_sum += v.z
	var n := float(rr.size())
	var cx := _corr(rr, gx)
	var cy := _corr(gg, gy)
	found["shading_match_x"] = snappedf(cx, 0.01)
	found["shading_match_y"] = snappedf(cy, 0.01)
	found["mean_length"] = snappedf(len_sum / n, 0.01)
	found["mean_z"] = snappedf(z_sum / n, 0.01)
	if z_sum / n < 0.5:
		warnings.append("blue (z) is low on average (%.2f): this may be a world- or object-space normal map, not a tangent-space one" % (z_sum / n))
	var flip := false
	if absf(cx) < 0.05 or absf(cy) < 0.05:
		found["convention"] = "unsure"
		warnings.append("cannot tell OpenGL from DirectX: the picture's shading barely matches the map; pass the map as-is and check under a raking light")
	elif signf(cx) == signf(cy):
		found["convention"] = "opengl"
	else:
		found["convention"] = "directx"
		flip = true
		changed.append("green flipped (DirectX -> OpenGL, which Godot expects)")
	var renorm := absf(len_sum / n - 1.0) > 0.03
	if renorm:
		changed.append("vectors normalized (mean length was %.2f)" % (len_sum / n))
	for y in img.get_height():
		for x in img.get_width():
			var c := img.get_pixel(x, y)
			var v := Vector3(c.r, c.g, c.b) * 2.0 - Vector3.ONE
			if flip:
				v.y = -v.y
			if v.z < 0.0:
				v.z = 0.0
			v = v.normalized() if v.length() > 1e-4 else Vector3(0, 0, 1)
			img.set_pixel(x, y, Color(v.x * 0.5 + 0.5, v.y * 0.5 + 0.5, v.z * 0.5 + 0.5))


func _fix_depth(img: Image, w: int, h: int, found: Dictionary, changed: Array, warnings: Array, depth_kind: String) -> void:
	var g := _chan_grid(img, w, h, 0)
	var lo := 1e9
	var hi := -1e9
	for v in g:
		lo = minf(lo, v)
		hi = maxf(hi, v)
	# In most scenes the bottom of the picture is nearer than the top (ground vs sky).
	var top := 0.0
	var bottom := 0.0
	var band := maxi(1, h / 5)
	for y in band:
		for x in w:
			top += g[y * w + x]
			bottom += g[(h - 1 - y) * w + x]
	top /= band * w
	bottom /= band * w
	var white_near := bottom > top
	found["bottom_minus_top"] = snappedf(bottom - top, 0.01)
	found["white_is"] = "near" if white_near else "far"
	if absf(bottom - top) < 0.05 * maxf(hi - lo, 1e-4):
		warnings.append("cannot tell whether white is near or far (bottom and top are alike); pass --depth-kind")
	var to_disparity := not white_near
	if depth_kind == "disparity":
		to_disparity = false
	elif depth_kind == "linear":
		to_disparity = true
	found["range_in"] = [snappedf(lo, 0.001), snappedf(hi, 0.001)]
	if to_disparity:
		changed.append("inverted so that white = near (what U.ZU Picture reads as disparity)")
	if hi - lo < 0.5:
		changed.append("stretched range %.2f..%.2f to 0..1" % [lo, hi])
	for y in img.get_height():
		for x in img.get_width():
			var v := (img.get_pixel(x, y).r - lo) / maxf(hi - lo, 1e-4)
			if to_disparity:
				v = 1.0 - v
			img.set_pixel(x, y, Color(v, v, v))
