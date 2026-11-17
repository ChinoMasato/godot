@tool
extends EditorPlugin

func _enter_tree():
	_verify.call_deferred()

func _find_menu(node):
	if node is PopupMenu:
		for i in range(node.get_item_count()):
			if node.get_item_text(i).contains("About U.ZU Engine"):
				return [node, node.get_item_id(i)]
	for child in node.get_children(true):
		var result = _find_menu(child)
		if not result.is_empty():
			return result
	return []

func _find_about(node):
	if node.get_class() == "EditorAbout" or (node is Window and node.title == "About U.ZU Engine"):
		return node
	for child in node.get_children(true):
		var result = _find_about(child)
		if result:
			return result
	return null

func _find_mark(node):
	if node is TextureRect and node.texture and node.texture.get_width() == node.texture.get_height():
		return node
	for child in node.get_children(true):
		var result = _find_mark(child)
		if result:
			return result
	return null

func _fail(message):
	push_error("UZU_VERIFY: " + message)
	if OS.get_environment("UZU_LOGO_AUTOCLOSE") == "1":
		get_tree().quit(1)

func _verify():
	await get_tree().create_timer(4.0).timeout
	var root = get_tree().root
	var menu = _find_menu(root)
	if menu.is_empty():
		_fail("About menu not found")
		return
	menu[0].emit_signal("id_pressed", menu[1])
	await get_tree().process_frame
	var about = _find_about(root)
	if not about:
		_fail("EditorAbout not found")
		return
	var mark = _find_mark(about)
	if not mark:
		_fail("logo TextureRect not found")
		return
	var samples = [mark.rotation]
	var started = about.is_processing_internal()
	await get_tree().create_timer(0.6).timeout
	samples.append(mark.rotation)
	await get_tree().create_timer(2.0).timeout
	samples.append(mark.rotation)
	var settled = not about.is_processing_internal() and is_zero_approx(mark.rotation)
	about.hide()
	await get_tree().process_frame
	about.popup_centered(Vector2i(780, 500))
	await get_tree().create_timer(0.2).timeout
	var restarted = about.is_processing_internal() and not is_zero_approx(mark.rotation)
	# Interrupt an active turn, rather than hiding an already settled dialog.
	about.hide()
	await get_tree().process_frame
	var hidden_stopped = not about.is_processing_internal()
	about.popup_centered(Vector2i(780, 500))
	await get_tree().create_timer(2.5).timeout
	var result = {
		"started": started,
		"sample_rotations": samples,
		"settled": settled,
		"hidden_stopped": hidden_stopped,
		"restarted": restarted,
		"passed": started and settled and hidden_stopped and restarted and samples[0] != samples[1],
	}
	var screenshot_path = OS.get_environment("UZU_LOGO_SCREENSHOT")
	if not screenshot_path.is_empty():
		mark.queue_redraw()
		await RenderingServer.frame_post_draw
		var screenshot = about.get_texture().get_image()
		if screenshot and not screenshot.is_empty():
			result["screenshot_error"] = screenshot.save_png(screenshot_path)
		else:
			result["screenshot_error"] = "empty viewport image"
	var report_path = OS.get_environment("UZU_LOGO_REPORT")
	if report_path.is_empty():
		report_path = "user://uzu-logo-smoke.json"
	var output = FileAccess.open(report_path, FileAccess.WRITE)
	if output == null:
		_fail("cannot write result to " + report_path)
		return
	output.store_string(JSON.stringify(result, "  "))
	output.close()
	print("UZU_VERIFY: ", JSON.stringify(result))
	if OS.get_environment("UZU_LOGO_AUTOCLOSE") == "1":
		await get_tree().create_timer(20.0).timeout
		get_tree().quit(0 if result["passed"] else 1)
