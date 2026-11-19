extends Node
## UI richness probe (BACKLOG B-1305). Installed as an autoload by
## `ai_loop/uzu.py look ui-report` in a copy of the project; the UI scene runs headless
## (layout and text shaping need no GPU). After a few frames it measures every visible
## Control and writes the report to the file given by "--uzu-ui-report <file> <frames>".

var _path := ""
var _frames := 5
var _seen := 0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-ui-report")
	if i < 0 or i + 2 >= args.size():
		set_process(false)
		return
	_path = args[i + 1]
	_frames = maxi(2, args[i + 2].to_int())
	# Headless runs ignore --resolution: set the screen size the layout is measured at.
	var s := args.find("--uzu-ui-size")
	if s >= 0 and s + 1 < args.size():
		var wh := args[s + 1].split("x")
		get_window().size = Vector2i(wh[0].to_int(), wh[1].to_int())


func _process(_delta: float) -> void:
	_seen += 1
	if _seen < _frames:
		return
	set_process(false)
	var f := FileAccess.open(_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(_measure()))
		f.close()
	get_tree().quit()


func _rect_list(r: Rect2) -> Array:
	return [roundi(r.position.x), roundi(r.position.y), roundi(r.size.x), roundi(r.size.y)]


func _font_size(c: Control) -> int:
	if c is RichTextLabel:
		return c.get_theme_font_size("normal_font_size")
	return c.get_theme_font_size("font_size")


## Whether the control's look comes from a styled theme (anything but the engine's default).
func _styled(c: Control, item: String, type: String) -> bool:
	if c.has_theme_stylebox_override(item):
		return true
	var n: Node = c
	while n != null:
		if n is Control and n.theme != null and n.theme.has_stylebox(item, type):
			return true
		n = n.get_parent()
	return false


## The child of the screen (the scene's root Control) that holds n.
func _branch(n: Node) -> Node:
	while n.get_parent() != null and n.get_parent().get_parent() != get_tree().root:
		n = n.get_parent()
	return n


func _measure() -> Dictionary:
	var screen := get_viewport().get_visible_rect()
	var controls := []
	for n in get_tree().root.find_children("*", "Control", true, false):
		var c := n as Control
		if c.is_visible_in_tree() and c.size.x > 0 and c.size.y > 0:
			controls.append(c)
	var by_type := {}
	var texts := 0
	var chars := 0
	var min_font := 0
	var depth := 0
	var problems := []
	var buttons := []
	var panels := []
	var unstyled := []
	# The screen covered by UI boxes (panels, buttons, lists, tabs; not the full-screen root or backdrops).
	var cols := 64
	var rows := 36
	var covered := {}
	for c in controls:
		var t: String = c.get_class()
		by_type[t] = by_type.get(t, 0) + 1
		var r: Rect2 = c.get_global_rect()
		var d := 0
		var p: Node = c.get_parent()
		while p is Control:
			d += 1
			p = p.get_parent()
		depth = maxi(depth, d)
		if c is Label or c is RichTextLabel or c is Button:
			var text: String = c.get_parsed_text() if c is RichTextLabel else c.text
			if not text.strip_edges().is_empty():
				texts += 1
				chars += text.length()
				var fs := _font_size(c)
				min_font = fs if min_font == 0 else mini(min_font, fs)
				if fs < 14:
					problems.append({"kind": "small_text", "node": str(c.get_path()), "font_size": fs, "fix": "font_size 16 or more (at 720p)"})
				if c is Label and c.autowrap_mode == TextServer.AUTOWRAP_OFF:
					# A label never gets smaller than its text: it silently grows past the size it was
					# given (outside a container, the offsets) or past its parent.
					var given: float = c.offset_right - c.offset_left if is_equal_approx(c.anchor_left, c.anchor_right) and not c.get_parent() is Container else r.size.x
					var parent_rect: Rect2 = c.get_parent().get_global_rect() if c.get_parent() is Control else screen
					if r.size.x > given + 1 or r.end.x > parent_rect.end.x + 1:
						problems.append({"kind": "text_overflow", "node": str(c.get_path()), "given_width": roundi(given), "text_width": roundi(r.size.x), "fix": "give it room (size or min_size) or let it wrap"})
		if c is BaseButton:
			buttons.append(c)
			if r.size.y < 44:
				problems.append({"kind": "small_button", "node": str(c.get_path()), "height": roundi(r.size.y), "fix": "44 px or taller for touch (min_size)"})
			if c is Button and not _styled(c, "normal", "Button"):
				unstyled.append(str(c.get_path()))
		var boxy: bool = c is PanelContainer or c is BaseButton or c is ItemList or c is TabContainer or c is ProgressBar
		if c is PanelContainer:
			panels.append(c)
			if not _styled(c, "panel", "PanelContainer"):
				unstyled.append(str(c.get_path()))
		if boxy and r.size.x * r.size.y < screen.size.x * screen.size.y * 0.9:
			for gy in rows:
				for gx in cols:
					var cell := Vector2((gx + 0.5) * screen.size.x / cols, (gy + 0.5) * screen.size.y / rows)
					if r.has_point(cell):
						covered[gy * cols + gx] = true
		if not screen.grow(1).encloses(r):
			problems.append({"kind": "off_screen", "node": str(c.get_path()), "rect": _rect_list(r), "fix": "anchor it inside the screen"})
	# Top-level panels: the edge inset and overlaps.
	var tops := []
	for pc in panels:
		var parent_panel := false
		var p: Node = pc.get_parent()
		while p is Control:
			if p is PanelContainer or p is TabContainer:
				parent_panel = true
			p = p.get_parent()
		if not parent_panel:
			tops.append(pc)
	var min_inset := -1.0
	for i in tops.size():
		var r: Rect2 = tops[i].get_global_rect()
		var inset := minf(minf(r.position.x, r.position.y), minf(screen.size.x - r.end.x, screen.size.y - r.end.y))
		min_inset = inset if min_inset < 0 else minf(min_inset, inset)
		if inset < 8:
			problems.append({"kind": "no_margin", "node": str(tops[i].get_path()), "inset": roundi(inset), "fix": "keep panels 16 px or more from the screen edge (offset)"})
		for j in range(i + 1, tops.size()):
			if _branch(tops[i]) == _branch(tops[j]):
				continue  # Parts of one made part (a talk window's name tag over its window) overlap on purpose.
			var other: Rect2 = tops[j].get_global_rect()
			if r.intersects(other) and r.intersection(other).get_area() > 4:
				problems.append({"kind": "overlap", "node": str(tops[i].get_path()), "with": str(tops[j].get_path()), "fix": "move one of them (anchor/offset)"})
	if not unstyled.is_empty():
		problems.append({"kind": "unstyled", "nodes": unstyled.slice(0, 8), "count": unstyled.size(), "fix": "give the screen a theme (uzu.py ui with a theme, or UZUUI.create_theme)"})
	var coverage := float(covered.size()) / (cols * rows)
	var hints := []
	if controls.size() > 0 and texts == 0:
		hints.append("no text on the screen")
	if coverage < 0.05 and not panels.is_empty():
		hints.append("the UI covers %d%% of the screen; that can be right for a HUD, but menus usually need more" % roundi(coverage * 100))
	if coverage > 0.6:
		hints.append("the UI covers %d%% of the screen; the game may be hard to see" % roundi(coverage * 100))
	if depth <= 2 and panels.size() <= 1 and texts > 3:
		hints.append("flat UI: texts without panels to group them; wrap related parts in a Panel")
	# Motion (B-1307 stage 2, owner's principle "something is always moving"): the world
	# decoration and the UI animations seen so far.
	var motion := {"worlds": [], "events": {}, "animating": UZUUIJuice.get_active_animation_count(), "moving_frames": 0}
	for w in get_tree().root.find_children("*", "UZUUIWorld", true, false):
		motion.worlds.append(w.get_stats())
	for e in UZUUIJuice.get_event_log():
		var key := "%s/%s" % [e.get("event", ""), e.get("phase", "")]
		motion.events[key] = motion.events.get(key, 0) + 1
	for c in controls:
		if c.material is ShaderMaterial and c.material.shader and c.material.shader.code.contains("TIME"):
			motion.moving_frames += 1
	if motion.worlds.is_empty() and motion.events.is_empty() and motion.moving_frames == 0 and not panels.is_empty():
		hints.append("nothing on the screen moves; a world (uzu.py ui --world, or a UZUUIWorld node) adds moving frames, opening and hover motion")
	var top_info := []
	for pc in tops:
		top_info.append({"node": str(pc.get_path()), "rect": _rect_list(pc.get_global_rect()), "children": pc.get_child_count()})
	return {"screen": [roundi(screen.size.x), roundi(screen.size.y)],
		"summary": {"controls": controls.size(), "panels": panels.size(), "buttons": buttons.size(), "texts": texts, "chars": chars,
			"min_font_size": min_font, "depth": depth, "coverage": snappedf(coverage, 0.001), "min_edge_inset": roundi(min_inset),
			"unstyled": unstyled.size()},
		"by_type": by_type, "problems": problems, "hints": hints, "panels": top_info, "motion": motion}
