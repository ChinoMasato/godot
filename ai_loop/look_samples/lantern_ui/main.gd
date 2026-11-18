extends Control

const LINES := [
	"川辺で、灯りの欠片がひとつ見つかったよ。\n残りはふたつ。風が止む前に探しておくれ。",
	"欠片がそろったら、石段の先にある祠へ。\nランタンに火をともせば、帰り道が見える。",
]
const IVORY := Color("#f4ead6")
const GOLD := Color("#e7c38d")

@export var frame_style: StyleBoxTexture
@export var dialog_font: Font
var page := 0
var margins: MarginContainer
var panel: PanelContainer
var body: Label
var action: Button


func _ready() -> void:
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
	stack.add_child(action)
	resized.connect(update_margins)
	update_margins()
	show_page()


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


func update_margins() -> void:
	body.add_theme_font_size_override("font_size", 18 if size.x < 540 else 21)
	var horizontal := maxi(16, roundi((size.x - 864) / 2.0))
	for side in ["left", "right"]:
		margins.add_theme_constant_override("margin_" + side, horizontal)
	for side in ["top", "bottom"]:
		margins.add_theme_constant_override("margin_" + side, 16 if size.x < 540 else 28)


func show_page() -> void:
	panel.visible = page < LINES.size()
	if panel.visible:
		body.text = LINES[page]
	action.text = ["次へ", "閉じる", "会話を開く"][page]


func advance() -> void:
	page = (page + 1) % (LINES.size() + 1)
	show_page()
