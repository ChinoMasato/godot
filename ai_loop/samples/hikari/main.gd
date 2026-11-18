extends Node3D
## 灯りの欠片 (Shards of Light) — U.ZU Engine all-features sample game.
##
## A five-minute HD-2D mini game: dusk falls on a small village, three light
## shards are scattered around it, and the player returns them to the shrine
## lantern. Built entirely from code (no editor GUI), lit by UZULook's HD-2D
## preset, with telemetry checkpoints and a time-of-day progression driven
## by the quest state.

const InputSetup := preload("res://input_setup.gd")
const FONT := preload("res://art/game_font.ttf")

const WORLD_X := 15.5
const WORLD_Z := 11.5
const PLAYER_SPEED := 3.4

var player: Sprite3D
var camera: Camera3D
var sun: DirectionalLight3D
var env: Environment
var lantern_light: OmniLight3D
var lantern_pos := Vector3(0, 0, -9.0)

var shards := 0
var cleared := false
var play_seconds := 0.0
var dialogue_lines: Array = []
var lantern_glow := 0.0

var obstacles: Array = [] # {pos: Vector2, r: float}
var interactables: Array = [] # {pos: Vector3, kind: String, id: String, label: String, node: Node3D}
var nearest_interactable: Dictionary = {}

var hud_shards: Label
var hud_objective: Label
var prompt_label: Label
var dialogue_panel: PanelContainer
var dialogue_name: Label
var dialogue_text: Label
var clear_panel: PanelContainer
var clear_text: Label

# Time-of-day stages, indexed by shards collected (0..3).
var stages: Array = [
	{"sun_rot": Vector3(-38, -30, 0), "sun_color": Color(1.0, 0.78, 0.55), "sun_energy": 1.15, "bg": Color(0.62, 0.55, 0.45), "amb": Color(0.55, 0.52, 0.48), "amb_e": 0.55},
	{"sun_rot": Vector3(-24, -42, 0), "sun_color": Color(1.0, 0.6, 0.38), "sun_energy": 0.95, "bg": Color(0.45, 0.36, 0.38), "amb": Color(0.45, 0.4, 0.45), "amb_e": 0.5},
	{"sun_rot": Vector3(-14, -55, 0), "sun_color": Color(0.85, 0.5, 0.45), "sun_energy": 0.7, "bg": Color(0.28, 0.24, 0.38), "amb": Color(0.36, 34.0 / 100.0, 0.45), "amb_e": 0.48},
	{"sun_rot": Vector3(-10, -65, 0), "sun_color": Color(0.55, 0.65, 0.95), "sun_energy": 0.4, "bg": Color(0.1, 0.12, 0.24), "amb": Color(0.3, 0.34, 0.48), "amb_e": 0.45},
]
var stage_current := {}
var stage_target := {}

func _ready() -> void:
	add_to_group("uzu_state")
	InputSetup.setup()
	_build_world()
	var report := UZULook.apply_hd2d(self, UZULook.ATMOSPHERE_GOLDEN_HOUR, false)
	print("UZU_SAMPLE look report: ", JSON.stringify(report))
	_find_sun_and_env()
	_build_ui()
	stage_current = stages[0].duplicate()
	stage_target = stages[0].duplicate()
	_apply_stage(stage_current, 1.0)
	UZUTelemetry.checkpoint("start")
	_set_objective("村に落ちた灯りの欠片を集めよう（0/3）")

## Declared state for the engine's state hash (B-307): what "the same game
## state" means for this game. The AI player / self-play tooling hashes this
## to judge a playthrough mechanically.
func uzu_state() -> Dictionary:
	var p: Array = []
	if player:
		p = [snappedf(player.global_position.x, 0.01), snappedf(player.global_position.z, 0.01)]
	return {"shards": shards, "cleared": cleared, "player": p}

func _process(delta: float) -> void:
	if not cleared:
		play_seconds += delta
	_update_player(delta)
	_update_camera(delta)
	_update_stage(delta)
	_update_interactable()
	if lantern_glow > 0.0 and lantern_light:
		lantern_light.light_energy = lerpf(lantern_light.light_energy, 2.6, delta * 1.5)

# ---------------------------------------------------------------- world

func _mat(color: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	return m

func _box(name: String, pos: Vector3, size: Vector3, color: Color, obstacle_r := 0.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = name
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.set_surface_override_material(0, _mat(color))
	mi.position = pos
	add_child(mi)
	if obstacle_r > 0.0:
		obstacles.append({"pos": Vector2(pos.x, pos.z), "r": obstacle_r})
	return mi

func _tree(pos: Vector3) -> void:
	var trunk := _box("Trunk", pos + Vector3(0, 0.5, 0), Vector3(0.35, 1.0, 0.35), Color(0.4, 0.29, 0.19))
	trunk.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	var cone := MeshInstance3D.new()
	cone.name = "TreeTop"
	var cm := CylinderMesh.new()
	cm.top_radius = 0.0
	cm.bottom_radius = 1.35
	cm.height = 3.1
	cone.mesh = cm
	cone.material_override = UZULook.create_wind_foliage_material(0.35, 1.1)
	cone.position = pos + Vector3(0, 2.5, 0)
	add_child(cone)
	obstacles.append({"pos": Vector2(pos.x, pos.z), "r": 0.65})

func _sprite(texture_path: String, pos: Vector3) -> Sprite3D:
	var s := Sprite3D.new()
	s.texture = load(texture_path)
	s.pixel_size = 0.06
	s.position = pos
	add_child(s)
	return s

func _build_world() -> void:
	# Ground and a dirt path.
	var ground := MeshInstance3D.new()
	ground.name = "Ground"
	var plane := PlaneMesh.new()
	plane.size = Vector2(34, 26)
	ground.mesh = plane
	ground.set_surface_override_material(0, _mat(Color(0.36, 0.47, 0.26)))
	add_child(ground)
	_box("Path", Vector3(0, 0.02, 1.5), Vector3(2.2, 0.04, 19), Color(0.55, 0.45, 0.32))
	_box("PathEW", Vector3(0, 0.02, -3.5), Vector3(24, 0.04, 2.0), Color(0.55, 0.45, 0.32))

	# Pond (south-east).
	var pond := MeshInstance3D.new()
	pond.name = "Pond"
	var disc := CylinderMesh.new()
	disc.top_radius = 2.4
	disc.bottom_radius = 2.4
	disc.height = 0.08
	pond.mesh = disc
	pond.set_surface_override_material(0, _mat(Color(0.25, 0.42, 0.62)))
	pond.position = Vector3(9.5, 0.04, 4.5)
	add_child(pond)
	obstacles.append({"pos": Vector2(9.5, 4.5), "r": 2.3})

	# Houses.
	_house(Vector3(-6.5, 0, -4.5), Color(0.85, 0.76, 0.62))
	_house(Vector3(6.0, 0, -5.5), Color(0.8, 0.72, 0.66))
	_house(Vector3(-9.0, 0, 3.5), Color(0.76, 0.68, 0.58))

	# Trees.
	for p in [Vector3(-4.2, 0, -7.6), Vector3(4.8, 0, -8.6), Vector3(-12.5, 0, -2.0), Vector3(12.8, 0, -1.0), Vector3(-11.5, 0, 8.5), Vector3(13.0, 0, 8.0), Vector3(2.5, 0, 6.5)]:
		_tree(p)

	# Shrine at the north edge: two pillars, a beam, and the lantern.
	_box("ShrinePillarL", Vector3(-1.3, 1.5, -9.6), Vector3(0.4, 3.0, 0.4), Color(0.45, 0.25, 0.2), 0.45)
	_box("ShrinePillarR", Vector3(1.3, 1.5, -9.6), Vector3(0.4, 3.0, 0.4), Color(0.45, 0.25, 0.2), 0.45)
	_box("ShrineBeam", Vector3(0, 3.1, -9.6), Vector3(3.6, 0.35, 0.5), Color(0.5, 0.28, 0.22))
	_box("LanternBase", Vector3(0, 0.3, -9.0), Vector3(0.7, 0.6, 0.7), Color(0.5, 0.5, 0.54), 0.8)
	_box("LanternBox", Vector3(0, 0.85, -9.0), Vector3(0.55, 0.5, 0.55), Color(0.85, 0.7, 0.4))
	lantern_light = OmniLight3D.new()
	lantern_light.position = Vector3(0, 1.0, -9.0)
	lantern_light.light_color = Color(1.0, 0.72, 0.38)
	lantern_light.light_energy = 0.0
	lantern_light.omni_range = 7.0
	add_child(lantern_light)
	interactables.append({"pos": lantern_pos, "kind": "lantern", "id": "lantern", "label": "E： ランタンに灯す", "node": null})

	# Crates near the east house; one shard sits by them.
	_box("Crate1", Vector3(7.2, 0.5, -0.8), Vector3(1.0, 1.0, 1.0), Color(0.62, 0.48, 0.33), 0.85)
	_box("Crate2", Vector3(7.3, 1.45, -0.9), Vector3(0.85, 0.85, 0.85), Color(0.58, 0.44, 0.3), 0.0)

	# Player.
	var flat_normal: Texture2D = load("res://art/flat_normal.png")
	player = _sprite("res://art/hero.png", Vector3(0, 0.72, 4.5))
	player.name = "Player"
	UZULook.configure_lit_sprite(player, flat_normal, 1.5)
	UZULook.add_blob_shadow(player, 0.5)

	# NPCs.
	_npc("npc_red", Vector3(2.2, 0.72, -7.6), "村長", [
		"おお、旅の子か。村の灯りが消えて三日になる。",
		"灯りの欠片が三つ、村に落ちたんじゃ。光っておるから分かるはず。",
		"集めたら祠のランタンに戻しておくれ。祭りが開ける。"])
	_npc("npc_green", Vector3(6.8, 0.72, 2.6), "ミドリ", [
		"池のほとりで何か光ってたよ。水辺は足元に気をつけてね。"])
	_npc("npc_purple", Vector3(-4.6, 0.72, 0.8), "スミレ", [
		"木の陰って暗いけど、よーく見ると光るものがあるんだ。",
		"北西の大きな木の裏、あやしいと思うなあ。"])

	# Light shards.
	_shard("shard_tree", Vector3(-4.9, 0.55, -8.6))
	_shard("shard_pond", Vector3(7.6, 0.55, 6.4))
	_shard("shard_crate", Vector3(6.1, 0.55, 0.6))

	camera = Camera3D.new()
	camera.name = "Camera3D"
	camera.fov = 50.0
	add_child(camera)
	camera.global_position = player.global_position + Vector3(0, 7.2, 9.2)
	camera.look_at(player.global_position + Vector3(0, 0.9, 0), Vector3.UP)

func _house(pos: Vector3, wall: Color) -> void:
	_box("House", pos + Vector3(0, 1.25, 0), Vector3(3.6, 2.5, 3.0), wall, 2.4)
	var roof := MeshInstance3D.new()
	roof.name = "Roof"
	var prism := PrismMesh.new()
	prism.left_to_right = 0.5
	prism.size = Vector3(4.2, 1.4, 3.6)
	roof.mesh = prism
	roof.set_surface_override_material(0, _mat(Color(0.5, 0.3, 0.24)))
	roof.position = pos + Vector3(0, 3.2, 0)
	add_child(roof)
	# A warm window that reads at dusk.
	var win := _box("Window", pos + Vector3(0.9, 1.4, 1.53), Vector3(0.7, 0.7, 0.05), Color(1.0, 0.75, 0.4))
	win.material_override = null

func _npc(texture_name: String, pos: Vector3, npc_name: String, lines: Array) -> void:
	var s := _sprite("res://art/" + texture_name + ".png", pos)
	s.name = "NPC_" + npc_name
	obstacles.append({"pos": Vector2(pos.x, pos.z), "r": 0.5})
	interactables.append({"pos": pos, "kind": "npc", "id": npc_name, "label": "E： 話す", "node": s, "lines": lines, "npc_name": npc_name})

func _shard(id: String, pos: Vector3) -> void:
	var s := _sprite("res://art/shard.png", pos)
	s.name = id
	s.pixel_size = 0.05
	var glow := OmniLight3D.new()
	glow.light_color = Color(0.55, 0.9, 1.0)
	glow.light_energy = 0.8
	glow.omni_range = 2.4
	glow.position = pos
	add_child(glow)
	var sparkles := UZULook.create_particles(UZULook.PARTICLES_EMBERS, Vector3(0.6, 0.8, 0.6))
	sparkles.position = pos
	add_child(sparkles)
	interactables.append({"pos": pos, "kind": "shard", "id": id, "label": "E： 欠片を拾う", "node": s, "glow": glow, "sparkles": sparkles})

# ---------------------------------------------------------------- runtime

func _find_sun_and_env() -> void:
	var stack: Array = [self]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		if n is DirectionalLight3D and sun == null and n.shadow_enabled:
			sun = n
		for c in n.get_children():
			stack.append(c)
	env = get_world_3d().environment
	if env == null:
		var we := get_node_or_null("WorldEnvironment") as WorldEnvironment
		if we:
			env = we.environment

func _update_player(delta: float) -> void:
	if not dialogue_lines.is_empty():
		return
	var input_vec := Input.get_vector("move_left", "move_right", "move_up", "move_down")
	if input_vec.length() > 0.0:
		var dir := Vector3(input_vec.x, 0, input_vec.y)
		player.global_position += dir * PLAYER_SPEED * delta
		player.global_position.y = 0.72 + abs(sin(Time.get_ticks_msec() / 90.0)) * 0.05
	else:
		player.global_position.y = 0.72
	var p := player.global_position
	p.x = clampf(p.x, -WORLD_X, WORLD_X)
	p.z = clampf(p.z, -WORLD_Z, WORLD_Z)
	for o in obstacles:
		var op: Vector2 = o["pos"]
		var d := Vector2(p.x, p.z) - op
		var min_d: float = o["r"] + 0.35
		if d.length() < min_d and d.length() > 0.001:
			d = d.normalized() * min_d
			p.x = op.x + d.x
			p.z = op.y + d.y
	player.global_position = p

func _update_camera(delta: float) -> void:
	var target := player.global_position + Vector3(0, 7.2, 9.2)
	camera.global_position = camera.global_position.lerp(target, minf(delta * 4.0, 1.0))
	camera.look_at(player.global_position + Vector3(0, 0.9, 0), Vector3.UP)

func _apply_stage(st: Dictionary, _w: float) -> void:
	if sun:
		sun.rotation_degrees = st["sun_rot"]
		sun.light_color = st["sun_color"]
		sun.light_energy = st["sun_energy"]
	if env:
		env.background_color = st["bg"]
		env.ambient_light_color = st["amb"]
		env.ambient_light_energy = st["amb_e"]

func _update_stage(delta: float) -> void:
	var w := minf(delta * 1.2, 1.0)
	for key in ["sun_energy", "amb_e"]:
		stage_current[key] = lerpf(float(stage_current[key]), float(stage_target[key]), w)
	for key in ["sun_color", "bg", "amb"]:
		stage_current[key] = (stage_current[key] as Color).lerp(stage_target[key], w)
	stage_current["sun_rot"] = (stage_current["sun_rot"] as Vector3).lerp(stage_target["sun_rot"], w)
	_apply_stage(stage_current, w)

func _update_interactable() -> void:
	nearest_interactable = {}
	var best := 1.7
	for it in interactables:
		if it["kind"] == "shard" and it["node"] == null:
			continue
		var d: float = player.global_position.distance_to(it["pos"])
		if d < best:
			best = d
			nearest_interactable = it
	if dialogue_lines.is_empty():
		prompt_label.text = nearest_interactable.get("label", "") if not nearest_interactable.is_empty() else ""

func _unhandled_input(event: InputEvent) -> void:
	if not event.is_action_pressed("interact"):
		return
	if not dialogue_lines.is_empty():
		_advance_dialogue()
		return
	if nearest_interactable.is_empty():
		return
	match nearest_interactable["kind"]:
		"npc":
			dialogue_lines = (nearest_interactable["lines"] as Array).duplicate()
			dialogue_name.text = nearest_interactable["npc_name"]
			dialogue_panel.visible = true
			_advance_dialogue()
		"shard":
			_collect_shard(nearest_interactable)
		"lantern":
			_touch_lantern()

func _advance_dialogue() -> void:
	if dialogue_lines.is_empty():
		dialogue_panel.visible = false
		return
	dialogue_text.text = dialogue_lines.pop_front()

func _collect_shard(it: Dictionary) -> void:
	shards += 1
	UZUTelemetry.checkpoint("shard_%d" % shards)
	if it["node"]:
		(it["node"] as Node).queue_free()
		it["node"] = null
	if it.get("glow"):
		(it["glow"] as Node).queue_free()
	if it.get("sparkles"):
		(it["sparkles"] as Node).queue_free()
	stage_target = stages[mini(shards, 3)].duplicate()
	hud_shards.text = "灯りの欠片 %d / 3" % shards
	if shards >= 3:
		_set_objective("祠のランタンに欠片を戻そう")
	else:
		_set_objective("村に落ちた灯りの欠片を集めよう（%d/3）" % shards)

func _touch_lantern() -> void:
	if cleared:
		return
	if shards < 3:
		dialogue_name.text = "ランタン"
		dialogue_lines = ["まだ暗い。欠片が %d つ足りない。" % (3 - shards)]
		dialogue_panel.visible = true
		_advance_dialogue()
		return
	cleared = true
	lantern_glow = 1.0
	UZUTelemetry.checkpoint("lantern_lit")
	UZUTelemetry.clear()
	var fireflies := UZULook.create_particles(UZULook.PARTICLES_FIREFLIES, Vector3(9, 2.5, 9))
	fireflies.position = Vector3(0, 1.2, -6.5)
	add_child(fireflies)
	_set_objective("村に灯りが戻った！")
	await get_tree().create_timer(1.4).timeout
	clear_text.text = "村に灯りが戻った！\nクリア時間 %d 分 %d 秒\n\n遊んでくれてありがとう。" % [int(play_seconds) / 60, int(play_seconds) % 60]
	clear_panel.visible = true

func _set_objective(text: String) -> void:
	hud_objective.text = text

# ---------------------------------------------------------------- UI

func _label(text: String, size: int) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", FONT)
	l.add_theme_font_size_override("font_size", size)
	return l

func _build_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	hud_shards = _label("灯りの欠片 0 / 3", 24)
	hud_shards.position = Vector2(18, 12)
	layer.add_child(hud_shards)
	hud_objective = _label("", 18)
	hud_objective.position = Vector2(18, 46)
	layer.add_child(hud_objective)
	var help := _label("矢印 / WASD： 移動　E： 話す・拾う・灯す", 15)
	help.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	help.position = Vector2(18, -34)
	layer.add_child(help)
	prompt_label = _label("", 22)
	prompt_label.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	prompt_label.position = Vector2(-150, -110)
	prompt_label.size = Vector2(300, 32)
	prompt_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(prompt_label)

	dialogue_panel = PanelContainer.new()
	dialogue_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	dialogue_panel.position = Vector2(60, -120)
	dialogue_panel.size = Vector2(-120, 100)
	dialogue_panel.visible = false
	layer.add_child(dialogue_panel)
	var vbox := VBoxContainer.new()
	dialogue_panel.add_child(vbox)
	dialogue_name = _label("", 17)
	vbox.add_child(dialogue_name)
	dialogue_text = _label("", 21)
	dialogue_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(dialogue_text)

	clear_panel = PanelContainer.new()
	clear_panel.set_anchors_preset(Control.PRESET_CENTER)
	clear_panel.position = Vector2(-190, -90)
	clear_panel.size = Vector2(380, 180)
	clear_panel.visible = false
	layer.add_child(clear_panel)
	clear_text = _label("", 24)
	clear_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	clear_panel.add_child(clear_text)
