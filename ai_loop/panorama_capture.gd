extends SceneTree
## Captures the six cube faces around the main scene's camera (BACKLOG B-713).
## Run with a real renderer (ai_loop/look.sh panorama does this with software
## Vulkan and a virtual display). Writes face_<i>.png and faces.json (camera
## bases) to the directory given after "--"; ai_loop/panorama.py joins them.

const FACE_SIZE := 512
const WARMUP_FRAMES := 8
# Forward direction and up vector of each face.
const FACES := [
	[Vector3(0, 0, -1), Vector3.UP], [Vector3(1, 0, 0), Vector3.UP], [Vector3(0, 0, 1), Vector3.UP],
	[Vector3(-1, 0, 0), Vector3.UP], [Vector3(0, 1, 0), Vector3(0, 0, 1)], [Vector3(0, -1, 0), Vector3(0, 0, -1)],
]

var out_dir := ""
var camera: Camera3D
var face := 0
var frames := 0
var bases := []


var main: Node
var started := false


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	out_dir = args[0] if args.size() > 0 else "user://panorama"
	DisplayServer.window_set_size(Vector2i(FACE_SIZE, FACE_SIZE))
	root.size = Vector2i(FACE_SIZE, FACE_SIZE)
	var scene := load(ProjectSettings.get_setting("application/run/main_scene", "")) as PackedScene
	if scene:
		main = scene.instantiate()
		root.add_child(main)


# Runs on the first frame, once the scene is in the tree.
func _start() -> bool:
	if main == null:
		printerr("panorama: the project has no loadable main scene")
		return false
	var source: Camera3D = null
	var cameras := main.find_children("*", "Camera3D", true, false)
	for c in cameras:
		if (c as Camera3D).current:
			source = c
	if source == null and cameras.size() > 0:
		source = cameras[0]
	camera = Camera3D.new()
	camera.fov = 90.0
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	main.add_child(camera)
	if source:
		camera.global_position = source.global_position
		camera.near = source.near
		camera.far = source.far
	camera.make_current()
	_aim()
	return true


func _aim() -> void:
	var f: Vector3 = FACES[face][0]
	camera.global_basis = Basis.looking_at(f, FACES[face][1])
	frames = 0


func _process(_delta: float) -> bool:
	if not started:
		started = true
		if not _start():
			return true  # Quit; no faces.json tells the caller it failed.
	frames += 1
	if frames < WARMUP_FRAMES:
		return false
	var image := root.get_texture().get_image()
	image.convert(Image.FORMAT_RGB8)
	image.save_png(out_dir.path_join("face_%d.png" % face))
	var b := camera.global_basis
	bases.append([[b.x.x, b.x.y, b.x.z], [b.y.x, b.y.y, b.y.z], [b.z.x, b.z.y, b.z.z]])
	face += 1
	if face >= FACES.size():
		var f := FileAccess.open(out_dir.path_join("faces.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify({"fov": 90.0, "size": FACE_SIZE, "bases": bases}))
		f.close()
		return true
	_aim()
	return false
