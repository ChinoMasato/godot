extends Control

const LINES := [
	"川辺で、灯りの欠片がひとつ見つかったよ。\n残りはふたつ。風が止む前に探しておくれ。",
	"欠片がそろったら、石段の先にある祠へ。\nランタンに火をともせば、帰り道が見える。",
]
const IVORY := Color("#f4ead6")
const GOLD := Color("#e7c38d")
const SHRINE_LANTERN := Vector2(0.599, 0.265)

@export var frame_style: StyleBoxTexture
@export var dialog_font: Font
var page := 0
var margins: MarginContainer
var panel: PanelContainer
var body: Label
var action: Button
var lantern_glow: TextureRect
var reduce_motion := false
var opening := false


func _ready() -> void:
	reduce_motion = OS.get_cmdline_user_args().has("--reduce-motion")
	create_lantern_glow()
	var ui_theme := Theme.new()
	ui_theme.default_font = dialog_font
	ui_theme.set_color("font_color", "Label", IVORY)
	theme = ui_theme

	margins = MarginContainer.new()
	add_child(margins)
	margins.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var stack := VBoxContainer.new()
	stack.add_theme_constant_override("separation", 12)
	margins.add_child(stack)

	var hud := HBoxContainer.new()
	stack.add_child(hud)
	hud.add_child(label_chip("灯りの村", 20))
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hud.add_child(gap)
	hud.add_child(label_chip("灯りの欠片  1 / 3", 16))

	var space := Control.new()
	space.size_flags_vertical = Control.SIZE_EXPAND_FILL
	stack.add_child(space)
	panel = PanelContainer.new()
	panel.name = "Dialogue"
	panel.add_theme_stylebox_override("panel", frame_style)
	stack.add_child(panel)
	var content := VBoxContainer.new()
	content.add_theme_constant_override("separation", 12)
	panel.add_child(content)
	var speaker := Label.new()
	speaker.text = "灯守"
	speaker.add_theme_color_override("font_color", GOLD)
	speaker.add_theme_font_size_override("font_size", 20)
	content.add_child(speaker)
	body = Label.new()
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.add_theme_constant_override("line_spacing", 6)
	content.add_child(body)

	action = Button.new()
	action.name = "DialogueAction"
	action.size_flags_horizontal = Control.SIZE_SHRINK_END
	action.custom_minimum_size = Vector2(128, 48)
	action.add_theme_font_size_override("font_size", 18)
	action.add_theme_color_override("font_color", IVORY)
	action.add_theme_color_override("font_hover_color", Color("#ffffff"))
	action.add_theme_stylebox_override("normal", flat(Color("#273a51"), Color("#9d784b"), 1))
	action.add_theme_stylebox_override("hover", flat(Color("#3e4d62"), GOLD, 1))
	action.add_theme_stylebox_override("pressed", flat(Color("#152538"), GOLD, 1))
	var focus := flat(Color.TRANSPARENT, GOLD, 2)
	focus.draw_center = false
	focus.set_expand_margin_all(3)
	action.add_theme_stylebox_override("focus", focus)
	action.pressed.connect(advance)
	action.mouse_entered.connect(pulse_action)
	action.focus_entered.connect(pulse_action)
	stack.add_child(action)
	resized.connect(update_margins)
	update_margins()
	show_page()
	open_dialogue()


func flat(fill: Color, border: Color, width: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(width)
	style.set_corner_radius_all(6)
	return style


func label_chip(title: String, size_px: int) -> PanelContainer:
	var chip := PanelContainer.new()
	var style := flat(Color("#101b2dea"), Color("#9d784b"), 1)
	style.content_margin_left = 12
	style.content_margin_right = 12
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	chip.add_theme_stylebox_override("panel", style)
	var label := Label.new()
	label.text = title
	label.add_theme_font_size_override("font_size", size_px)
	chip.add_child(label)
	return chip


func create_lantern_glow() -> void:
	var colors := Gradient.new()
	colors.colors = PackedColorArray([Color("#ffd58f"), Color("#ffd58f00")])
	var texture := GradientTexture2D.new()
	texture.gradient = colors
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = 64
	texture.height = 64
	lantern_glow = TextureRect.new()
	lantern_glow.name = "ShrineGlow"
	lantern_glow.texture = texture
	lantern_glow.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	lantern_glow.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lantern_glow.modulate.a = 0.16
	var additive := CanvasItemMaterial.new()
	additive.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	lantern_glow.material = additive
	add_child(lantern_glow)
	if not reduce_motion:
		var flicker := create_tween().set_loops().set_trans(Tween.TRANS_SINE)
		flicker.tween_property(lantern_glow, "modulate:a", 0.38, 1.05)
		flicker.tween_property(lantern_glow, "modulate:a", 0.16, 1.35)


func update_lantern_glow() -> void:
	var image_size: Vector2 = $Village.texture.get_size()
	var cover := maxf(size.x / image_size.x, size.y / image_size.y)
	var drawn := image_size * cover
	lantern_glow.size = Vector2.ONE * 68.0 * cover
	lantern_glow.position = (size - drawn) * 0.5 + drawn * SHRINE_LANTERN - lantern_glow.size * 0.5


func update_margins() -> void:
	update_lantern_glow()
	body.add_theme_font_size_override("font_size", 18 if size.x < 540 else 21)
	var horizontal := maxi(16, roundi((size.x - 864) / 2.0))
	for side in ["left", "right"]:
		margins.add_theme_constant_override("margin_" + side, horizontal)
	for side in ["top", "bottom"]:
		margins.add_theme_constant_override("margin_" + side, 16 if size.x < 540 else 28)


func show_page() -> void:
	if page < LINES.size():
		body.text = LINES[page]
	action.text = ["次へ", "閉じる", "会話を開く"][page]


func open_dialogue() -> void:
	panel.show()
	if reduce_motion:
		return
	opening = true
	# Give the container a layout pass before the animation reads the panel size.
	await get_tree().process_frame
	UZUUIJuice.apply_world_motion(panel, "wafu")
	opening = false


func pulse_action() -> void:
	if not reduce_motion and not UZUUIJuice.is_animating(action):
		UZUUIJuice.hover_pulse(action, 0.35, 0.16)


func advance() -> void:
	if opening or UZUUIJuice.is_animating(panel):
		return
	page = (page + 1) % (LINES.size() + 1)
	show_page()
	pulse_action()
	if page == LINES.size():
		if reduce_motion:
			panel.hide()
		else:
			UZUUIJuice.apply_world_motion(panel, "wafu", false)
	elif page == 0:
		open_dialogue()
