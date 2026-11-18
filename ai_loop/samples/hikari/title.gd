extends Node3D
## Title screen: a U.ZU Picture (one illustration + depth map, turned into a
## lit parallax relief by the engine) with the game title over it.
## Any interact input starts the game.

const InputSetup := preload("res://input_setup.gd")

func _ready() -> void:
	InputSetup.setup()
	var layer := CanvasLayer.new()
	add_child(layer)
	var title := Label.new()
	title.text = "灯りの欠片"
	title.add_theme_font_size_override("font_size", 64)
	title.set_anchors_preset(Control.PRESET_CENTER_TOP)
	title.position = Vector2(-160, 90)
	title.size = Vector2(320, 80)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(title)
	var sub := Label.new()
	sub.text = "U.ZU Engine サンプルゲーム"
	sub.add_theme_font_size_override("font_size", 20)
	sub.set_anchors_preset(Control.PRESET_CENTER_TOP)
	sub.position = Vector2(-160, 165)
	sub.size = Vector2(320, 30)
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(sub)
	var hint := Label.new()
	hint.text = "E / Space : はじめる"
	hint.add_theme_font_size_override("font_size", 22)
	hint.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	hint.position = Vector2(-160, -70)
	hint.size = Vector2(320, 30)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(hint)

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("interact"):
		get_tree().change_scene_to_file("res://main.tscn")
