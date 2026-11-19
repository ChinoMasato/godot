extends SceneTree

func _initialize() -> void:
	verify.call_deferred()

func click(position: Vector2) -> void:
	for down in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.position = position
		event.global_position = position
		event.pressed = down
		root.push_input(event)

func accept() -> void:
	for down in [true, false]:
		var event := InputEventKey.new()
		event.keycode = KEY_ENTER
		event.pressed = down
		root.push_input(event)

func settle(scene) -> bool:
	# Wait for engine-driven animations, including a deferred open after layout.
	var deadline := Time.get_ticks_msec() + 2500
	while scene.opening or UZUUIJuice.get_active_animation_count() > 0:
		if Time.get_ticks_msec() >= deadline:
			return false
		await process_frame
	await process_frame
	return true


func motion_events_match(reduced: bool) -> bool:
	var phases := {}
	for entry in UZUUIJuice.get_event_log():
		var signal_name: String = entry.target + "/" + entry.event + "/" + entry.phase
		phases[signal_name] = phases.get(signal_name, 0) + 1
	if reduced:
		return phases.is_empty()
	return (
		phases.get("Dialogue/open/start") == 2
		and phases.get("Dialogue/open/end") == 2
		and phases.get("Dialogue/close/start") == 1
		and phases.get("Dialogue/close/end") == 1
		and phases.get("DialogueAction/hover/start", 0) > 0
		and phases.get("DialogueAction/hover/start") == phases.get("DialogueAction/hover/end")
	)


func verify() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		push_error("Supply a screenshot path after --")
		quit(2)
		return
	UZUUIJuice.clear_event_log()
	var scene := preload("res://main.tscn").instantiate()
	root.add_child(scene)
	var first_glow: float = scene.lantern_glow.modulate.a
	var settled := await settle(scene)
	await create_timer(0.45).timeout
	var glow_changed: bool = absf(first_glow - scene.lantern_glow.modulate.a) > 0.02
	var glow_ok: bool = glow_changed != scene.reduce_motion
	await RenderingServer.frame_post_draw
	var saved := root.get_texture().get_image().save_png(args[0]) == OK
	var view := root.get_visible_rect()
	var body_rect: Rect2 = scene.body.get_global_rect()
	var fits: bool = view.encloses(body_rect) and view.encloses(scene.action.get_global_rect()) and body_rect.end.y < scene.action.get_global_rect().position.y
	var button: Button = scene.action
	button.grab_focus()
	settled = await settle(scene) and settled
	accept()
	settled = await settle(scene) and settled
	var next_ok: bool = scene.page == 1 and scene.body.text == scene.LINES[1]
	click(button.get_global_rect().get_center())
	# Repeated activation during close must not restart or reopen the dialogue.
	if not scene.reduce_motion:
		accept()
	settled = await settle(scene) and settled
	var closed: bool = scene.page == 2 and not scene.panel.visible and button.has_focus()
	click(button.get_global_rect().get_center())
	settled = await settle(scene) and settled
	var reopened: bool = scene.page == 0 and scene.panel.visible and scene.panel.scale.is_equal_approx(Vector2.ONE)
	var events_ok := motion_events_match(scene.reduce_motion)
	var ok: bool = saved and fits and next_ok and closed and reopened and events_ok and glow_ok and settled
	print("UI_RESULT " + JSON.stringify({
		"ok": ok, "size": [view.size.x, view.size.y], "fits": fits,
		"next": next_ok, "close": closed, "reopen": reopened, "saved": saved,
		"settled": settled, "motion_events": events_ok, "glow": glow_ok,
		"reduced_motion": scene.reduce_motion, "events": UZUUIJuice.get_event_log(),
	}))
	quit(0 if ok else 1)
