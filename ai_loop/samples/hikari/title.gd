extends Node3D
## Title screen: a U.ZU Picture (one illustration + depth map, turned into a
## lit parallax relief by the engine) with the game title over it.
## Any interact input starts the game.

const InputSetup := preload("res://input_setup.gd")
const FONT := preload("res://art/game_font.ttf")

func _style(l: Label, size: int) -> void:
	# The subset font must be set explicitly: the default font has no
	# Japanese glyphs, and exports (Web especially) get no system fallback.
	l.add_theme_font_override("font", FONT)
	l.add_theme_font_size_override("font_size", size)

func _ready() -> void:
	InputSetup.setup()
	# Fallback: the flat illustration on a quad behind the UZUPicture relief,
	# so the title still shows the art on GPUs/browsers where the picture's
	# shaders do not run (seen on iOS Safari).
	var flat := Sprite3D.new()
	flat.texture = load("res://title/art.png")
	flat.pixel_size = 0.0354
	flat.position = Vector3(0, 0, -0.6)
	add_child(flat)
	var layer := CanvasLayer.new()
	add_child(layer)
	var title := Label.new()
	title.text = "灯りの欠片"
	_style(title, 64)
	title.set_anchors_preset(Control.PRESET_CENTER_TOP)
	title.position = Vector2(-160, 90)
	title.size = Vector2(320, 80)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(title)
	var sub := Label.new()
	sub.text = "U.ZU Engine サンプルゲーム"
	_style(sub, 20)
	sub.set_anchors_preset(Control.PRESET_CENTER_TOP)
	sub.position = Vector2(-160, 165)
	sub.size = Vector2(320, 30)
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(sub)
	var hint := Label.new()
	hint.text = "E / Space : はじめる"
	_style(hint, 22)
	hint.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	hint.position = Vector2(-160, -70)
	hint.size = Vector2(320, 30)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(hint)

func _unhandled_input(event: InputEvent) -> void:
	# Keys, taps and clicks all start the game (phones have no keyboard).
	var start := event.is_action_pressed("interact")
	if event is InputEventScreenTouch and (event as InputEventScreenTouch).pressed:
		start = true
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
		start = true
	if start:
		get_tree().change_scene_to_file("res://main.tscn")
