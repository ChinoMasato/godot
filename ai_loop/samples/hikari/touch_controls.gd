extends Control
## Touch controls (B-1004): the virtual stick (bottom-left) and the interact
## button (bottom-right) shown on touch devices, or when forced for checks
## (see main.gd's _touch_ui_enabled). Built in code like the rest of the
## sample; the button inherits the UZUUI world theme of the UI root, and its
## label follows the nearest interactable, mirroring the on-screen prompt.

signal interact_pressed

const VirtualStick := preload("res://virtual_stick.gd")

var stick: Control
var interact_button: Button

func _init() -> void:
	name = "TouchControls"
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE

func _ready() -> void:
	# Placement is anchors + offsets, set before adding: setting position
	# after add_child does not land where the anchors suggest (the same
	# pattern as the dialogue window in main.gd).
	stick = VirtualStick.new()
	stick.name = "Stick"
	stick.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	stick.offset_left = 36
	stick.offset_top = -204
	stick.offset_right = 204
	stick.offset_bottom = -36
	add_child(stick)

	interact_button = Button.new()
	interact_button.name = "InteractButton"
	interact_button.text = "決定"
	interact_button.focus_mode = Control.FOCUS_NONE # Space/Enter stay the game's interact key.
	interact_button.add_theme_font_size_override("font_size", 20)
	interact_button.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	interact_button.offset_left = -140
	interact_button.offset_top = -140
	interact_button.offset_right = -36
	interact_button.offset_bottom = -36
	interact_button.pressed.connect(func() -> void: interact_pressed.emit())
	add_child(interact_button)

## The button's label mirrors the prompt ("話す", "欠片を拾う", ...). With
## nothing in reach it falls back to a dimmed 決定.
func set_interact_text(text: String) -> void:
	if not interact_button:
		return
	interact_button.text = text if not text.is_empty() else "決定"
	interact_button.modulate.a = 1.0 if not text.is_empty() else 0.55
