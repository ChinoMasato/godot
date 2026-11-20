extends SceneTree
## Bakes the generated village of 『灯りの欠片』 into a binary scene the
## game loads at start instead of generating: the three generator calls
## (~700 ms on the dev VM) become one ~10 ms scene load, navigation mesh
## included. The scene root carries the world fingerprint (the three
## specs + the build version in main.gd); the game adopts the scene only
## while the stamp matches and falls back to runtime generation
## otherwise, so this file is a cache, never a second source of truth.
##
##   godot --headless --path <hikari> --script bake_world.gd
##   -> writes res://baked_village.scn, prints UZU_RESULT {...}
##
## Re-run after changing any world spec or the build steps in main.gd
## (the steps' shape is guarded by WORLD_BUILD_VERSION there).

func _initialize() -> void:
	# Loaded at runtime, not preloaded: main.gd names the UZUTelemetry
	# autoload, which is only a known identifier once the project is up.
	var Main := load("res://main.gd") as GDScript
	var root := Node3D.new()
	root.name = "BakedWorld"
	get_root().add_child(root)
	var t0 := Time.get_ticks_msec()
	var built: Dictionary = Main.build_generated_world(root)
	var generate_ms := Time.get_ticks_msec() - t0
	root.set_meta("uzu_baked_fingerprint", Main.world_fingerprint())
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err != OK:
		print("UZU_RESULT {\"ok\": false, \"error\": \"pack failed (%d)\"}" % err)
		quit(1)
		return
	t0 = Time.get_ticks_msec()
	err = ResourceSaver.save(packed, "res://baked_village.scn")
	var save_ms := Time.get_ticks_msec() - t0
	if err != OK:
		print("UZU_RESULT {\"ok\": false, \"error\": \"save failed (%d)\"}" % err)
		quit(1)
		return
	var city_result: Dictionary = built["city_result"]
	print("UZU_RESULT ", JSON.stringify({
		"ok": true, "scene": "res://baked_village.scn",
		"fingerprint": Main.world_fingerprint(),
		"generate_ms": generate_ms, "save_ms": save_ms,
		"city_counts": city_result.get("counts", {}),
		"navigation": city_result.get("navigation", {}),
		"land_y": (built["land"] as Node3D).position.y,
	}))
	quit()
