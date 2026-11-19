extends SceneTree
## Dumps the generation record of the generated village (B-1508) for the
## spec set (B-1509): the game's own three specs from main.gd
## (land_spec / pond_spec / city_spec), each generated on its own, with the
## full UZUWorldGen result of each. Driven by make_spec_set.py, which
## turns the record into the four spec-set documents under res://spec_set/.
##
##   godot --headless --path <hikari> --script compose_spec_set.gd -- --record /abs/record.json
##
## The record describes the three generations, not the game's runtime
## adjustments after them (main.gd levels the land by the city peak, trims
## scatter out of the play area, cuts the pond terrain at the city border
## and adds a foundation slab); those belong to the game, not to the
## generators, and are named in the record's "context".

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var i := args.find("--record")
	if i < 0 or i + 1 >= args.size():
		print("UZU_RESULT {\"ok\": false, \"error\": \"needs --record /abs/record.json\"}")
		quit(1)
		return
	var record_path := args[i + 1]

	# Loaded at runtime, not preloaded: main.gd names the UZUTelemetry
	# autoload, which is only a known identifier once the project is up.
	var Main := load("res://main.gd") as GDScript
	var root := Node3D.new()
	root.name = "SpecSetVillage"
	get_root().add_child(root)

	var parts := []
	var all_ok := true
	for entry in [
		["nature", Main.land_spec(), "main.gd land_spec()"],
		["nature", Main.pond_spec(), "main.gd pond_spec()"],
		["city", Main.city_spec(), "main.gd city_spec()"],
	]:
		var kind: String = entry[0]
		var spec: Dictionary = entry[1]
		var result: Dictionary = UZUWorldGen.generate_nature(root, spec) if kind == "nature" else UZUWorldGen.generate_city(root, spec)
		result.erase("overview") # An Image; not JSON data.
		all_ok = all_ok and bool(result.get("ok", false))
		parts.append({
			"kind": "map",
			"generator": "UZUWorldGen.generate_" + kind,
			"scene": "res://village.tscn",
			"spec_source": entry[2],
			"input_spec": spec,
			"result": result,
		})

	var record := {
		"kind": "composite",
		"generator": "UZUWorldGen via ai_loop/samples/hikari/compose_spec_set.gd (the game's own specs from main.gd)",
		"scene": "res://village.tscn",
		"spec_source": "ai_loop/samples/hikari/main.gd (land_spec/pond_spec/city_spec)",
		"context": "The village of B-1508 is three generations composed by main.gd: Land (nature, seed 172) under everything, "
			+ "Pond (nature, seed 61) to the east, Village (city, preset village, seed 1, ground=false) on top. "
			+ "This record holds the three generations as specified; the game's own adjustments after generation "
			+ "(levelling the land by the city peak, trimming scatter and terrain, the foundation slab under the "
			+ "streets) are main.gd's, not the generators', and are not part of the record.",
		"parts": parts,
	}
	var f := FileAccess.open(record_path, FileAccess.WRITE)
	if f == null:
		print("UZU_RESULT {\"ok\": false, \"error\": \"could not write %s\"}" % record_path)
		quit(1)
		return
	f.store_string(JSON.stringify(record, "\t"))
	f.close()
	print("UZU_RESULT ", JSON.stringify({"ok": all_ok, "record": record_path, "parts": parts.size()}))
	quit(0 if all_ok else 1)
