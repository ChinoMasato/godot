extends Node3D
## Title screen: a U.ZU Picture (one illustration + depth map, turned into a
## lit parallax relief by the engine) with the game title over it.
## Any interact input starts the game.
##
## The overlay is a declarative UI build (B-1301): ui/title_ui.json is the
## source, ui/title_ui.tscn is generated from it with
##   python3 ai_loop/uzu.py ui <this project> --spec ui/title_ui.json
## It uses the wafu world theme (B-1307), the same world as the game's UI,
## and its UZUUIWorld opens the plaques with the world's quiet motion.

const InputSetup := preload("res://input_setup.gd")
const FONT := preload("res://art/game_font.ttf")

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
	var packed := load("res://ui/title_ui.tscn") as PackedScene
	if packed:
		var ui := packed.instantiate() as Control
		layer.add_child(ui)
		# Taps anywhere must reach _unhandled_input: nothing in the overlay
		# may swallow them.
		ui.mouse_filter = Control.MOUSE_FILTER_IGNORE
		for c in ui.find_children("*", "Control", true, false):
			(c as Control).mouse_filter = Control.MOUSE_FILTER_IGNORE
	else:
		# The generated scene is committed next to its spec; this is only a
		# guard so the title never comes up blank.
		var title := Label.new()
		title.text = "灯りの欠片"
		title.add_theme_font_override("font", FONT)
		title.add_theme_font_size_override("font_size", 64)
		title.set_anchors_preset(Control.PRESET_CENTER_TOP)
		title.position = Vector2(-160, 90)
		title.size = Vector2(320, 80)
		title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		layer.add_child(title)

func _unhandled_input(event: InputEvent) -> void:
	# Keys, taps and clicks all start the game (phones have no keyboard).
	var start := event.is_action_pressed("interact")
	if event is InputEventScreenTouch and (event as InputEventScreenTouch).pressed:
		start = true
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
		start = true
	if start:
		get_tree().change_scene_to_file("res://main.tscn")
