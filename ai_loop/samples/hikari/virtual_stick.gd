extends Control
## Virtual stick (B-1004): a touch joystick drawn in code, in the sample's
## wafu colours (a translucent parchment base, a vermilion knob). While a
## finger drags inside it, it feeds the move_left / move_right / move_up /
## move_down actions with analog strength, so the game's Input.get_vector
## polling picks touch movement up unchanged. The mouse works too, which
## keeps the stick checkable on a desktop.

const DEADZONE := 0.22
const RADIUS := 84.0
const KNOB_RADIUS := 34.0

var value := Vector2.ZERO
var _drag := -1 # -1: idle, -2: mouse, >= 0: touch index
var _knob_offset := Vector2.ZERO

func _ready() -> void:
	custom_minimum_size = Vector2(RADIUS * 2.0, RADIUS * 2.0)
	mouse_filter = Control.MOUSE_FILTER_STOP

func _gui_input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		var t := event as InputEventScreenTouch
		if t.pressed and _drag == -1:
			_drag = t.index
			_update_knob(t.position)
			accept_event()
		elif not t.pressed and t.index == _drag:
			_release()
			accept_event()
	elif event is InputEventScreenDrag:
		var d := event as InputEventScreenDrag
		if d.index == _drag:
			_update_knob(d.position)
			accept_event()
	elif event is InputEventMouseButton:
		var b := event as InputEventMouseButton
		if b.button_index == MOUSE_BUTTON_LEFT:
			if b.pressed and _drag == -1:
				_drag = -2
				_update_knob(b.position)
				accept_event()
			elif not b.pressed and _drag == -2:
				_release()
				accept_event()
	elif event is InputEventMouseMotion and _drag == -2:
		_update_knob((event as InputEventMouseMotion).position)
		accept_event()

func _process(_delta: float) -> void:
	# A mouse release outside the control never reaches _gui_input; do not
	# leave the stick held.
	if _drag == -2 and not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		_release()

func _notification(what: int) -> void:
	if what == NOTIFICATION_EXIT_TREE:
		_release()

func _update_knob(local_pos: Vector2) -> void:
	var max_r := RADIUS - 6.0
	var off := local_pos - size * 0.5
	if off.length() > max_r:
		off = off.normalized() * max_r
	_knob_offset = off
	var v := off / max_r
	if v.length() < DEADZONE:
		value = Vector2.ZERO
	else:
		var m := clampf((v.length() - DEADZONE) / (1.0 - DEADZONE), 0.0, 1.0)
		value = v.normalized() * m
	_apply_actions()
	queue_redraw()

func _release() -> void:
	_drag = -1
	value = Vector2.ZERO
	_knob_offset = Vector2.ZERO
	_apply_actions()
	queue_redraw()

func _apply_actions() -> void:
	_set_action("move_right", value.x)
	_set_action("move_left", -value.x)
	_set_action("move_down", value.y)
	_set_action("move_up", -value.y)

func _set_action(action: StringName, strength: float) -> void:
	if strength > 0.05:
		Input.action_press(action, strength)
	else:
		Input.action_release(action)

func _draw() -> void:
	var c := size * 0.5
	draw_circle(c, RADIUS, Color(0.97, 0.91, 0.78, 0.30))
	draw_arc(c, RADIUS - 1.5, 0.0, TAU, 48, Color(0.23, 0.14, 0.09, 0.70), 3.0)
	var knob := c + _knob_offset
	draw_circle(knob, KNOB_RADIUS, Color(0.72, 0.20, 0.16, 0.80))
	draw_arc(knob, KNOB_RADIUS - 1.0, 0.0, TAU, 40, Color(0.23, 0.08, 0.06, 0.85), 2.0)
