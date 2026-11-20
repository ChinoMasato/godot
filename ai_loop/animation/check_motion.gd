extends SceneTree
## Verify that a packed animated character plays while moving in a scene.

func _initialize() -> void:
	check.call_deferred()


func finish(result: Dictionary) -> void:
	print("MOTION_CHECK " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


func check() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		finish({"ok": false, "error": "Pass an animated .tscn path."})
		return
	var scene := load(args[0]) as PackedScene
	if scene == null:
		finish({"ok": false, "error": "Could not load animated scene."})
		return
	var actor := CharacterBody3D.new()
	actor.motion_mode = CharacterBody3D.MOTION_MODE_FLOATING
	root.add_child(actor)
	var character := scene.instantiate()
	actor.add_child(character)
	var player := character.get_node_or_null("MotionPlayer") as AnimationPlayer
	var skeleton := character.find_children("*", "Skeleton3D", true, false)[0] as Skeleton3D
	var leg := skeleton.find_bone("Thigh.L")
	if player == null or leg < 0:
		finish({"ok": false, "error": "Expected the default left thigh and generated animation player."})
		return
	player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	var clips := {}
	for name: String in ["idle", "walk", "run"]:
		if not player.has_animation(name):
			finish({"ok": false, "error": "Missing clip: " + name})
			return
		var clip := player.get_animation(name)
		player.play(name)
		player.advance(0)
		player.seek(0.0, true)
		var first := skeleton.get_bone_pose_rotation(leg)
		player.seek(clip.length * 0.25, true)
		var middle := skeleton.get_bone_pose_rotation(leg)
		player.seek(clip.length - 0.0001, true)
		var last := skeleton.get_bone_pose_rotation(leg)
		var rotation := rad_to_deg(first.angle_to(middle))
		var seam := rad_to_deg(first.angle_to(last))
		if clip.loop_mode != Animation.LOOP_LINEAR or rotation < 1.0 or seam > 0.1:
			finish({"ok": false, "error": "Invalid motion or seam: " + name, "motion_deg": rotation, "seam_deg": seam})
			return
		player.advance(0.001)
		if player.current_animation_position >= 0.001:
			finish({"ok": false, "error": "Clip did not wrap: " + name})
			return
		clips[name] = {"motion_deg": rotation, "runtime_seam_deg": seam}

	player.play("walk")
	player.advance(0)
	player.seek(0, true)
	var start := actor.position
	var initial_pose := skeleton.get_bone_pose_rotation(leg)
	var walk_swing := 0.0
	for tick in range(75):
		await physics_frame
		actor.velocity = Vector3.FORWARD * 1.5
		actor.move_and_slide()
		player.advance(1.0 / Engine.physics_ticks_per_second)
		walk_swing = maxf(walk_swing, initial_pose.angle_to(skeleton.get_bone_pose_rotation(leg)))
	var distance := actor.position.distance_to(start)
	finish({
		"ok": distance > 1.0 and walk_swing > 0.25,
		"clips": clips, "walk_distance_m": distance,
		"walk_swing_deg": rad_to_deg(walk_swing),
		"walk_animation": player.current_animation,
		"walk_time_after_wrap_s": player.current_animation_position,
	})
