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

func verify() -> void:
	var scene := preload("res://main.tscn").instantiate()
	root.add_child(scene)
	await process_frame
	await RenderingServer.frame_post_draw
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		push_error("Supply a screenshot path after --")
		quit(2)
		return
	var shot := args[0]
	var saved := root.get_texture().get_image().save_png(shot) == OK
	var view := root.get_visible_rect()
	var fits: bool = view.encloses(scene.body.get_global_rect()) and view.encloses(scene.action.get_global_rect()) and scene.body.get_global_rect().end.y < scene.action.get_global_rect().position.y
	var button: Button = scene.action
	button.grab_focus()
	accept()
	await process_frame
	var next_ok: bool = scene.page == 1 and scene.body.text == scene.LINES[1]
	click(button.get_global_rect().get_center())
	await process_frame
	var closed: bool = scene.page == 2 and not scene.panel.visible and button.has_focus()
	click(button.get_global_rect().get_center())
	await process_frame
	var reopened: bool = scene.page == 0 and scene.panel.visible
	var ok: bool = saved and fits and next_ok and closed and reopened
	print("UI_RESULT " + JSON.stringify({"ok": ok, "size": [view.size.x, view.size.y], "fits": fits, "next": next_ok, "close": closed, "reopen": reopened, "saved": saved}))
	quit(0 if ok else 1)
