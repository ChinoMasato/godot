extends SceneTree
## Builds the generated village of 『灯りの欠片』 (B-1508) as a standalone
## scene — the same specs and composition as the game (main.gd's
## build_generated_world) — for the walking check and top-down captures:
##
##   godot --headless --path <hikari> --script compose_village.gd
##   -> saves res://village.tscn, prints UZU_RESULT {...}
##
## Then: python3 ai_loop/uzu.py map <hikari> walk --scene res://village.tscn
## The city's holder carries the uzu_places meta the walker reads.

func _initialize() -> void:
	# Loaded at runtime, not preloaded: main.gd names the UZUTelemetry
	# autoload, which is only a known identifier once the project is up.
	var Main := load("res://main.gd") as GDScript
	var root := Node3D.new()
	root.name = "VillageScene"
	get_root().add_child(root)
	var t0 := Time.get_ticks_msec()
	var built = Main.build_generated_world(root)
	var generate_ms := Time.get_ticks_msec() - t0
	var city_result: Dictionary = built["city_result"]
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err != OK:
		print("UZU_RESULT {\"ok\": false, \"error\": \"pack failed (%d)\"}" % err)
		quit(1)
		return
	err = ResourceSaver.save(packed, "res://village.tscn")
	if err != OK:
		print("UZU_RESULT {\"ok\": false, \"error\": \"save failed (%d)\"}" % err)
		quit(1)
		return
	print("UZU_RESULT ", JSON.stringify({
		"ok": true, "scene": "res://village.tscn",
		"generate_ms": generate_ms,
		"city_counts": city_result.get("counts", {}),
		"navigation": city_result.get("navigation", {}),
		"land_y": (built["land"] as Node3D).position.y,
	}))
	quit()
