extends SceneTree
## Generate in-place animation clips for a rigged model or align a PNG sequence.

const DEFAULT_BONES := {
	"root": "Root",
	"left_thigh": "Thigh.L", "right_thigh": "Thigh.R",
	"left_shin": "Shin.L", "right_shin": "Shin.R",
	"left_arm": "UpperArm.L", "right_arm": "UpperArm.R",
}
const CLIPS := {
	"idle": Vector4(2.0, 3.0, 2.0, 5.0),
	"walk": Vector4(1.0, 28.0, 48.0, 22.0),
	"run": Vector4(0.65, 48.0, 75.0, 38.0),
}
const INTERVALS := 32


func _initialize() -> void:
	build.call_deferred()


func build() -> void:
	var args := OS.get_cmdline_user_args()
	var options: Variant = JSON.parse_string(args[0]) if args.size() == 1 else null
	if not options is Dictionary:
		finish({"ok": false, "error": "Expected a JSON options object."})
		return
	var result := align_frames(options) if options.command == "align" else build_motion(options)
	finish(result)


func finish(result: Dictionary) -> void:
	print("UZU_ANIMATION " + JSON.stringify(result))
	quit(0 if result.get("ok", false) else 1)


func build_motion(options: Dictionary) -> Dictionary:
	var source := load(options.model) as PackedScene
	if source == null:
		return {"ok": false, "error": "Could not load the model as a scene. Import the GLB first."}
	var model := source.instantiate()
	var skeletons := model.find_children("*", "Skeleton3D", true, false)
	if model is Skeleton3D:
		skeletons.push_front(model)
	if skeletons.size() != 1:
		model.free()
		return {"ok": false, "error": "The model must have exactly one Skeleton3D."}
	var skeleton := skeletons[0] as Skeleton3D
	var overrides: Dictionary = options.get("bones", {})
	for role: String in overrides:
		if not DEFAULT_BONES.has(role):
			model.free()
			return {"ok": false, "error": "Unknown bone role: " + role}
	var mapping := DEFAULT_BONES.duplicate()
	mapping.merge(overrides, true)
	var joints := {}
	for role: String in DEFAULT_BONES:
		var bone := skeleton.find_bone(mapping[role])
		if bone < 0 or joints.values().has(bone):
			model.free()
			return {"ok": false, "error": "Missing or reused joint for " + role + ": " + mapping[role]}
		joints[role] = bone

	var character := Node3D.new()
	character.name = "AnimatedCharacter"
	var player := AnimationPlayer.new()
	player.name = "MotionPlayer"
	character.add_child(player)
	player.owner = character
	character.add_child(model, true)
	model.owner = character
	var library := AnimationLibrary.new()
	var report := {}
	for clip_name: String in CLIPS:
		var clip := create_clip(clip_name, character, skeleton, joints, options)
		library.add_animation(clip_name, clip)
		report[clip_name] = clip_report(clip)
	player.add_animation_library(&"", library)
	var packed := PackedScene.new()
	var status := packed.pack(character)
	if status == OK:
		status = ResourceSaver.save(packed, options.out)
	character.free()
	if status != OK:
		return {"ok": false, "error": "Could not save the animation scene: " + error_string(status)}
	return {"ok": true, "scene": options.out, "clips": report}


func create_clip(clip_name: String, character: Node, skeleton: Skeleton3D, joints: Dictionary, options: Dictionary) -> Animation:
	var profile: Vector4 = CLIPS[clip_name]
	var idle := clip_name == "idle"
	var clip := Animation.new()
	clip.length = profile.x if idle else profile.x / float(options.speed)
	clip.loop_mode = Animation.LOOP_LINEAR
	var skel_path := String(character.get_path_to(skeleton))
	for role: String in joints:
		if role == "root":
			continue
		var joint: int = joints[role]
		var track := clip.add_track(Animation.TYPE_ROTATION_3D)
		clip.track_set_path(track, NodePath(skel_path + ":" + String(skeleton.get_bone_name(joint))))
		# Express the rig's lateral axis in this bone's rest space.
		var basis := skeleton.get_bone_global_rest(joint).basis.orthonormalized()
		var axis := (basis.inverse() * Vector3.RIGHT).normalized()
		var rest := skeleton.get_bone_rest(joint).basis.orthonormalized().get_rotation_quaternion()
		for index in range(INTERVALS + 1):
			var phase := TAU * float(index % INTERVALS) / INTERVALS
			if role.begins_with("right"):
				phase += PI
			var swing := sin(phase)
			var angle := -profile.y * swing
			if role.ends_with("shin"):
				angle = profile.z * maxf(0.0, swing) if not idle else 0.0
			elif role.ends_with("arm"):
				angle = profile.w * swing
			if not idle:
				angle *= float(options.stride)
			clip.rotation_track_insert_key(track, clip.length * float(index) / INTERVALS,
					(rest * Quaternion(axis, deg_to_rad(angle))).normalized())

	var root_joint: int = joints.root
	var root_track := clip.add_track(Animation.TYPE_POSITION_3D)
	clip.track_set_path(root_track, NodePath(skel_path + ":" + String(skeleton.get_bone_name(root_joint))))
	var root_position := skeleton.get_bone_rest(root_joint).origin
	var parent := skeleton.get_bone_parent(root_joint)
	var up := Vector3.UP
	if parent >= 0:
		up = skeleton.get_bone_global_rest(parent).basis.inverse() * up
	var lift := (0.015 if idle else 0.035 if clip_name == "walk" else 0.07)
	# Heavy characters have a smaller vertical bounce.
	lift *= lerpf(1.25, 0.55, float(options.weight))
	for index in range(INTERVALS + 1):
		var phase := TAU * float(index % INTERVALS) / INTERVALS
		var bounce := (1.0 - cos(phase if idle else phase * 2.0)) * 0.5 * lift
		clip.position_track_insert_key(root_track, clip.length * float(index) / INTERVALS, root_position + up * bounce)
	return clip


func clip_report(clip: Animation) -> Dictionary:
	var rotation := 0.0
	var position := 0.0
	for track in range(clip.get_track_count()):
		var first: Variant = clip.track_get_key_value(track, 0)
		var last: Variant = clip.track_get_key_value(track, clip.track_get_key_count(track) - 1)
		if first is Quaternion:
			rotation = maxf(rotation, rad_to_deg(first.angle_to(last)))
		elif first is Vector3:
			position = maxf(position, first.distance_to(last))
	return {"seconds": clip.length, "rotation_seam_deg": rotation, "position_seam_m": position}


func align_frames(options: Dictionary) -> Dictionary:
	var directory: String = options.frames_dir
	if not DirAccess.dir_exists_absolute(directory):
		return {"ok": false, "error": "Frame directory does not exist."}
	var names := DirAccess.get_files_at(directory)
	var frames: Array[Dictionary] = []
	var canvas := Vector2i.ZERO
	for name: String in names:
		if name.get_extension().to_lower() != "png":
			continue
		var image := Image.load_from_file(directory.path_join(name))
		if image == null or image.is_empty():
			return {"ok": false, "error": "Could not read PNG: " + name}
		image.convert(Image.FORMAT_RGBA8)
		var content := image.get_used_rect()
		if not content.has_area():
			return {"ok": false, "error": "Frame has no visible pixels: " + name}
		canvas = canvas.max(content.size)
		frames.append({"name": name, "content": content})
	if frames.is_empty():
		return {"ok": false, "error": "Expected at least one PNG frame."}

	var output: String = options.out
	var status := DirAccess.make_dir_absolute(output)
	if status != OK:
		return {"ok": false, "error": "Could not create output directory: " + error_string(status)}
	var written := PackedStringArray()
	for frame: Dictionary in frames:
		var content: Rect2i = frame.content
		@warning_ignore("integer_division")
		var origin := Vector2i((canvas.x - content.size.x) / 2, canvas.y - content.size.y)
		var image := Image.load_from_file(directory.path_join(frame.name))
		if image != null and not image.is_empty():
			image.convert(Image.FORMAT_RGBA8)
		if image == null or image.is_empty() or image.get_used_rect() != content:
			status = ERR_FILE_CANT_READ
		else:
			var aligned := Image.create_empty(canvas.x, canvas.y, false, Image.FORMAT_RGBA8)
			aligned.blit_rect(image, content, origin)
			var path := output.path_join(frame.name)
			written.append(path)
			status = aligned.save_png(path)
		if status != OK:
			for path: String in written:
				DirAccess.remove_absolute(path)
			DirAccess.remove_absolute(output)
			return {"ok": false, "error": "Could not align " + frame.name + ": " + error_string(status)}
		frame.content = [content.position.x, content.position.y, content.size.x, content.size.y]
		frame.offset = [origin.x, origin.y]
	return {"ok": true, "canvas": [canvas.x, canvas.y], "frames": frames, "directory": output}
