extends Node2D
## A one-line runner: hold move_right, jump over the pits. Deterministic: all logic
## runs per physics frame from the input actions only, so replayed input gives
## the same play. Reports to UZUTelemetry when it is installed (ai_loop/selfplay.sh).

const SPEED := 4.0
const JUMP_FRAMES := 24
const PITS := [Vector2(300, 360), Vector2(700, 780)]
const CHECKPOINTS := {200.0: "cp1", 500.0: "cp2", 900.0: "cp3"}
const GOAL := 1000.0

var respawn_x := 0.0
var air := 0
var reached := {}
var done := false

@onready var player: Node2D = $Player


func _telemetry(method: String, arg = null) -> void:
	var t := get_node_or_null("/root/UZUTelemetry")
	if t:
		if arg == null:
			t.call(method)
		else:
			t.call(method, arg)


func _physics_process(_delta: float) -> void:
	if done:
		return
	if air > 0:
		air -= 1
	elif Input.is_action_just_pressed("jump"):
		air = JUMP_FRAMES
	if Input.is_action_pressed("move_right"):
		player.position.x += SPEED
	var x := player.position.x
	for pit in PITS:
		if air == 0 and x >= pit.x and x < pit.y:
			_telemetry("death", "pit")
			player.position.x = respawn_x
			return
	for cx in CHECKPOINTS:
		if x >= cx and not reached.has(cx):
			reached[cx] = true
			respawn_x = cx
			_telemetry("checkpoint", CHECKPOINTS[cx])
	if x >= GOAL:
		done = true
		_telemetry("clear")
		get_tree().quit()


func _draw() -> void:
	draw_line(Vector2(0, 8), Vector2(GOAL, 8), Color.WHITE, 2)
	for pit in PITS:
		draw_line(Vector2(pit.x, 8), Vector2(pit.y, 8), Color.BLACK, 4)
