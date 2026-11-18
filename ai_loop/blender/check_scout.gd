extends SceneTree
## Run after importing the generated scout.glb into the test project.

func _initialize() -> void:
	check.call_deferred()


func fail(message: String) -> void:
	print("SCOUT_RESULT " + JSON.stringify({"ok": false, "error": message}))
	quit(1)


func check() -> void:
	var packed := load("res://scout.glb") as PackedScene
	if packed == null:
		fail("scout.glb could not be loaded")
		return
	var model := packed.instantiate()
	root.add_child(model)
	var skeletons := model.find_children("*", "Skeleton3D", true, false)
	var players := model.find_children("*", "AnimationPlayer", true, false)
	var meshes := model.find_children("*", "MeshInstance3D", true, false)
	if skeletons.size() != 1 or players.size() != 1 or meshes.size() != 1:
		fail("expected one skeleton, one animation player and one mesh")
		return
	var skeleton := skeletons[0] as Skeleton3D
	var player := players[0] as AnimationPlayer
	var mesh := meshes[0] as MeshInstance3D
	if skeleton.get_bone_count() != 11 or mesh.skin == null:
		fail("expected an 11-bone skeleton and a skinned mesh")
		return
	var clips := {}
	for clip_name in ["IdleLoop", "WalkLoop"]:
		if not player.has_animation(clip_name):
			fail("missing animation: " + clip_name)
			return
		var animation := player.get_animation(clip_name)
		if animation.loop_mode != Animation.LOOP_LINEAR:
			fail("animation is not set to loop: " + clip_name)
			return
		var joint := skeleton.find_bone("Head" if clip_name == "IdleLoop" else "Thigh.L")
		if joint < 0:
			fail("animation joint missing")
			return
		player.play(clip_name)
		player.seek(0.0, true)
		var first := skeleton.get_bone_pose_rotation(joint)
		player.seek(animation.length * 0.5, true)
		var middle := skeleton.get_bone_pose_rotation(joint)
		player.seek(animation.length - 0.001, true)
		var last := skeleton.get_bone_pose_rotation(joint)
		var motion := first.angle_to(middle)
		if motion < 0.1 or first.angle_to(last) > 0.035:
			fail("animation is static or its loop seam does not match: " + clip_name)
			return
		clips[clip_name] = {"seconds": animation.length, "joint_motion_degrees": rad_to_deg(motion)}
	print("SCOUT_RESULT " + JSON.stringify({"ok": true, "bones": skeleton.get_bone_count(), "clips": clips}))
	quit()
