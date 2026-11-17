extends SceneTree
## Agent command line for U.ZU Picture. Prints one line "UZU_RESULT {json}"; exit 0 on success.
##
##   godot --headless --path P --script picture_tool.gd -- analyze  [--spec res://picture.json] [--overlay out.png] [--max 12]
##       Reads the picture and depth. Returns light candidates (bright warm spots, with pixel position,
##       color and distance), the sky share, and the depth range. --overlay writes the picture with each
##       candidate ringed (ring color = "mark" in the JSON) so an agent can look and pick.
##   godot --headless --path P --script picture_tool.gd -- validate [--spec res://picture.json]
##       Checks the spec: files exist, pixel points are inside the picture, known keys. Lists every problem.
##   godot --headless --path P --script picture_tool.gd -- adopt --ids 0,2,5 [--spec ...] [--energy 1.5]
##       Writes the chosen analyze candidates into the spec's "lights" (keeps the rest of the spec).
##   godot --headless --path P --script picture_tool.gd -- schema
##       Describes every spec key.
## Render: godot --path P --rendering-driver vulkan --write-movie out/f.png --fixed-fps 30 --quit-after 60

const SCHEMA := {
	"image": "res://picture (png/webp/jpg). Pixel coordinates below are in this image: x right, y down.",
	"depth": {"path": "res://depth map, grayscale", "kind": "disparity (white = near, default) | linear (white = far)", "near": "distance of the nearest point (default 4.5)", "far": "distance of the farthest point (default 160)"},
	"fov": "vertical field of view of the picture in degrees (default 38)",
	"normal": "optional res://normal map (picture space: x right, y up, z toward viewer)",
	"regions": {"<name>": {"path": "res://mask", "channel": "r|g|b|a"}, "names": "glow (painted lights that flicker), wet (shiny stone), sky (clouds drift, not relit), flicker (fire), waterfall (pulls down), sway (banners, foliage), water (ripples)"},
	"lights": [{"at": "[x, y] pixel of the light source", "color": "#rrggbb", "energy": "1.0", "flicker": "0..1", "reach": "range multiplier (default 1)"}],
	"moon": {"at": "[x, y]", "color": "#rrggbb", "energy": "0.6"},
	"effects": {"rain": {"amount": 1600, "opacity": 0.14}, "embers": {"from": "[[x, y], ...]", "amount": 48}, "fog": {"density": "0..1 multiplier of the look's fog", "layers": "[distances] mist sheets that drift past each other, e.g. [9, 18, 40, 90]", "amount": 0.4, "top": "mist sits below this (-1..1)"}, "clouds": {"amount": "0..1 extra cloud layers over the sky region", "speed": 1, "direction": "[x, y] in picture space"}, "lightning": {"first": "seconds until the first strike (to catch it in a shot)", "every": "[min, max] seconds", "strength": 1, "at": "[x, y] where the light comes from", "seed": 1}},
	"relief": {"tear": "split layers where the depth value jumps more than this (default 0.05; 0 = one sheet)", "backdrop": "true = background layer behind torn edges", "backdrop_radius": 6},
	"camera": {"drift": "sideways sway (default 1, 0 = still)", "push": "slow push-in (default 1)", "overscan": "share of the picture kept out of view at the edges (default 0.07; raise it with more drift)"},
	"look": {"atmosphere": "clear|humid|golden_hour|night", "quality": "high|standard|low", "post": "true = edge and vignette", "edge": 0.15, "vignette": 0.4, "bloom": 0.8},
}
const KNOWN := ["image", "depth", "fov", "normal", "regions", "lights", "moon", "effects", "camera", "look", "relief"]
const MARKS := ["#ff3030", "#30ff30", "#3080ff", "#ffff30", "#ff30ff", "#30ffff", "#ff9030", "#ffffff", "#9030ff", "#30ff90", "#ff3090", "#90ff30"]


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var result := _run(args)
	print("UZU_RESULT " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


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
		return {"ok": false, "error": "command needed: analyze | validate | adopt | schema"}
	var spec_path := _opt(args, "spec", "res://picture.json")
	match args[0]:
		"schema":
			return {"ok": true, "schema": SCHEMA}
		"validate":
			return _validate(spec_path)
		"analyze":
			return _analyze(spec_path, _opt(args, "overlay"), _opt(args, "max", "12").to_int())
		"adopt":
			return _adopt(spec_path, _opt(args, "ids"), _opt(args, "at"), _opt(args, "energy", "1.5").to_float())
		"snap":
			var spec := _read_spec(spec_path)
			var pts := []
			for p in _points(_opt(args, "at")):
				pts.append(_snap(spec, p, _opt(args, "radius", "40").to_int()))
			return {"ok": not pts.is_empty(), "points": pts, "error": "" if pts else "snap needs --at x,y[;x,y...]"}
	return {"ok": false, "error": "unknown command '%s'" % args[0]}


func _read_spec(path: String) -> Dictionary:
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed if parsed is Dictionary else {}


func _validate(path: String) -> Dictionary:
	var spec := _read_spec(path)
	if spec.is_empty():
		return {"ok": false, "problems": ["cannot read or parse %s" % path]}
	var problems := []
	for k in spec.keys():
		if not k in KNOWN:
			problems.append("unknown key '%s' (known: %s)" % [k, ", ".join(KNOWN)])
	if not spec.has("image") or not FileAccess.file_exists(spec.image):
		return {"ok": false, "problems": problems + ["image missing or not found"]}
	var size := _load(spec.image).get_size()
	if not spec.has("depth") or not FileAccess.file_exists(spec.depth.get("path", "")):
		problems.append("depth.path missing or not found")
	if spec.has("normal") and not FileAccess.file_exists(spec.normal):
		problems.append("normal not found: %s" % spec.normal)
	for name in spec.get("regions", {}):
		var r: Dictionary = spec.regions[name]
		if not FileAccess.file_exists(r.get("path", "")):
			problems.append("regions.%s.path not found" % name)
		if not r.get("channel", "r") in ["r", "g", "b", "a"]:
			problems.append("regions.%s.channel must be r, g, b or a" % name)
	var check_at := func(where: String, at) -> void:
		if not (at is Array and at.size() == 2):
			problems.append("%s.at must be [x, y]" % where)
		elif at[0] < 0 or at[1] < 0 or at[0] >= size.x or at[1] >= size.y:
			problems.append("%s.at %s is outside the picture (%dx%d)" % [where, str(at), size.x, size.y])
	var i := 0
	for l in spec.get("lights", []):
		check_at.call("lights[%d]" % i, l.get("at"))
		if l.has("color") and not Color.html_is_valid(l.color):
			problems.append("lights[%d].color is not #rrggbb" % i)
		i += 1
	if spec.has("moon"):
		check_at.call("moon", spec.moon.get("at"))
	var j := 0
	for at in spec.get("effects", {}).get("embers", {}).get("from", []):
		check_at.call("effects.embers.from[%d]" % j, at)
		j += 1
	return {"ok": problems.is_empty(), "picture": [size.x, size.y], "lights": spec.get("lights", []).size(), "problems": problems}


func _analyze(path: String, overlay: String, max_count: int) -> Dictionary:
	var spec := _read_spec(path)
	if spec.is_empty() or not spec.has("image"):
		return {"ok": false, "error": "spec needs at least \"image\""}
	var art := _load(spec.image)
	var size := art.get_size()
	# Work on a small copy. A light source is a small spot much brighter than its surroundings
	# (a lantern), not a large bright area (lit foliage, a bright wall): score = brightness above the
	# local mean, favouring warm colours.
	var w := 320
	var h := int(round(w * float(size.y) / size.x))
	var small: Image = art.duplicate()
	small.resize(w, h, Image.INTERPOLATE_BILINEAR)
	var around: Image = small.duplicate()
	around.resize(w / 16, h / 16, Image.INTERPOLATE_BILINEAR)
	around.resize(w, h, Image.INTERPOLATE_BILINEAR)
	var score := PackedFloat32Array()
	score.resize(w * h)
	var sky_px := 0
	for y in h:
		for x in w:
			var c := small.get_pixel(x, y)
			var lum: float = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
			var a := around.get_pixel(x, y)
			var mean: float = 0.2126 * a.r + 0.7152 * a.g + 0.0722 * a.b
			var warm := clampf((c.r - c.b) * 2.0, 0.0, 1.0)
			score[y * w + x] = maxf(0.0, lum - mean * 1.15) * lum * (0.35 + 0.65 * warm)
	var depth: Image = null
	if spec.has("depth") and FileAccess.file_exists(spec.depth.get("path", "")):
		depth = _load(spec.depth.path)
		depth.resize(w, h, Image.INTERPOLATE_BILINEAR)
		for y in h:
			for x in w:
				if depth.get_pixel(x, y).r < 0.04:
					sky_px += 1
	var cands := []
	var r := 4
	for y in range(r, h - r):
		for x in range(r, w - r):
			var s := score[y * w + x]
			if s < 0.04:
				continue
			var is_max := true
			for dy in range(-r, r + 1):
				for dx in range(-r, r + 1):
					if (dx or dy) and score[(y + dy) * w + x + dx] > s:
						is_max = false
						break
				if not is_max:
					break
			if is_max:
				cands.append({"x": x, "y": y, "s": s})
	cands.sort_custom(func(a, b): return a.s > b.s)
	var out := []
	var scale := float(size.x) / w
	for c in cands.slice(0, max_count):
		var px := Vector2i(int((c.x + 0.5) * scale), int((c.y + 0.5) * scale))
		var col := art.get_pixel(px.x, px.y)
		var entry := {"id": out.size(), "at": [px.x, px.y], "color": "#" + col.to_html(false), "strength": snappedf(c.s, 0.01), "mark": MARKS[out.size() % MARKS.size()]}
		if depth:
			entry["depth"] = snappedf(depth.get_pixel(c.x, c.y).r, 0.01)
		out.append(entry)
	if not overlay.is_empty():
		var o: Image = art.duplicate()
		o.convert(Image.FORMAT_RGBA8)
		for e in out:
			_ring(o, Vector2i(e.at[0], e.at[1]), 18, Color.html(e.mark))
		o.save_png(overlay)
	return {"ok": true, "picture": [size.x, size.y], "sky_share": snappedf(float(sky_px) / (w * h), 0.01) if depth else null,
		"candidates": out, "overlay": overlay, "next": "pick ids that are real light sources and run: adopt --ids 0,2"}


func _ring(img: Image, c: Vector2i, rad: int, col: Color) -> void:
	for a in 180:
		var ang := a * TAU / 180.0
		for t in 3:
			var p := c + Vector2i(roundi(cos(ang) * (rad + t)), roundi(sin(ang) * (rad + t)))
			if p.x >= 0 and p.y >= 0 and p.x < img.get_width() and p.y < img.get_height():
				img.set_pixelv(p, col)


## "x,y;x,y" → [Vector2i, ...]
func _points(text: String) -> Array:
	var out := []
	for part in text.split(";", false):
		var xy := part.split(",")
		if xy.size() == 2:
			out.append(Vector2i(xy[0].to_int(), xy[1].to_int()))
	return out


## Moves a rough point to the brightest warm pixel within radius; returns it with color and depth.
func _snap(spec: Dictionary, p: Vector2i, radius: int) -> Dictionary:
	var art := _load(spec.image)
	var best := p
	var best_s := -1.0
	for y in range(maxi(0, p.y - radius), mini(art.get_height(), p.y + radius + 1), 2):
		for x in range(maxi(0, p.x - radius), mini(art.get_width(), p.x + radius + 1), 2):
			if (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) > radius * radius:
				continue
			var c := art.get_pixel(x, y)
			var s: float = (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) * (0.6 + 0.4 * clampf((c.r - c.b) * 2.0, 0.0, 1.0))
			if s > best_s:
				best_s = s
				best = Vector2i(x, y)
	var out := {"asked": [p.x, p.y], "at": [best.x, best.y], "moved": snappedf(Vector2(best - p).length(), 0.1), "color": "#" + art.get_pixelv(best).to_html(false)}
	if spec.has("depth") and FileAccess.file_exists(spec.depth.get("path", "")):
		var d := _load(spec.depth.path)
		out["depth"] = snappedf(d.get_pixel(best.x * d.get_width() / art.get_width(), best.y * d.get_height() / art.get_height()).r, 0.01)
	return out


func _adopt(path: String, ids: String, at: String, energy: float) -> Dictionary:
	if ids.is_empty() and at.is_empty():
		return {"ok": false, "error": "adopt needs --ids 0,2,5 (from analyze) and/or --at x,y;x,y (rough points, snapped)"}
	var res := _analyze(path, "", 64)
	if not res.ok:
		return res
	var spec := _read_spec(path)
	var lights := []
	for s in ids.split(",", false):
		var i := s.strip_edges().to_int()
		if i < 0 or i >= res.candidates.size():
			return {"ok": false, "error": "no candidate %d" % i}
		var c: Dictionary = res.candidates[i]
		lights.append({"at": c.at, "color": c.color, "energy": energy, "flicker": 0.5})
	for p in _points(at):
		var s := _snap(spec, p, 40)
		lights.append({"at": s.at, "color": s.color, "energy": energy, "flicker": 0.5})
	spec["lights"] = lights
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(spec, "\t"))
	return {"ok": true, "lights": lights}
