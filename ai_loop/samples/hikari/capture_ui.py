#!/usr/bin/env python3
"""UI captures of the sample game (B-1306): runs a copy of the project with
a small driver autoload and saves PNGs of the title screen, the in-game HUD,
and an open dialogue. With UZU_TOUCH_UI=1 in the environment the same run
shows the touch controls (B-1004).

Rendering is the repo's software path (Xvfb + lavapipe Vulkan, via
ai_loop/uzu.py); what is judged here is the UI, not the 3D look.

Usage: python3 capture_ui.py <project-dir> <out-dir> <prefix>
Writes <prefix>_title.png, <prefix>_hud.png, <prefix>_dialogue.png.
Exit code 0 when every capture was written.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
import uzu  # noqa: E402

DRIVER = r'''
extends Node
## Capture driver (B-1306): installed as an autoload in a throwaway copy of
## the project. Title -> start -> HUD -> walk to the elder -> dialogue.

var _frame := 0
var _stage := 0
var _out := ""

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var args := OS.get_cmdline_user_args()
	var i := args.find("--uzu-capture")
	if i >= 0 and i + 1 < args.size():
		_out = args[i + 1]

func _save(name: String) -> void:
	get_viewport().get_texture().get_image().save_png(_out + "/" + name)

func _press_interact() -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = "interact"
		ev.pressed = pressed
		Input.parse_input_event(ev)

func _process(_delta: float) -> void:
	if _out.is_empty():
		return
	_frame += 1
	match _stage:
		0:
			if _frame >= 50:
				_save("title.png")
				_press_interact()
				_stage = 1
				_frame = 0
		1:
			if _frame >= 70:
				_save("hud.png")
				# Stand just south of the elder (2.2, -7.6), inside his
				# interact radius and outside his obstacle radius.
				get_tree().current_scene.player.global_position = Vector3(2.2, 0.72, -6.1)
				_stage = 2
				_frame = 0
		2:
			if _frame >= 20:
				_press_interact()
				_stage = 3
				_frame = 0
		3:
			if _frame >= 45:
				_save("dialogue.png")
				get_tree().quit()
'''


def main():
    project, out_dir, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="uzu_capture_")
    try:
        game = os.path.join(work, "game")
        shutil.copytree(project, game, ignore=shutil.ignore_patterns(".godot"))
        addon = os.path.join(game, "addons", "uzu_capture")
        os.makedirs(addon, exist_ok=True)
        with open(os.path.join(addon, "driver.gd"), "w", encoding="utf-8") as f:
            f.write(DRIVER)
        cfg = os.path.join(game, "project.godot")
        with open(cfg, encoding="utf-8") as f:
            text = f.read()
        entry = 'UZUCapture="*res://addons/uzu_capture/driver.gd"\n'
        text = text.replace("\n[autoload]\n", "\n[autoload]\n\n" + entry, 1) if "\n[autoload]\n" in text else text + "\n[autoload]\n\n" + entry
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(text)
        subprocess.run([uzu.find_godot(), "--headless", "--path", game, "--import"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        stage = os.path.join(work, "stage")
        os.makedirs(stage)
        cmd = [uzu.find_godot(), "--path", game, "--fixed-fps", "60", "--resolution", "1152x648"]
        cmd, env = uzu.render_command(cmd + uzu.driver(), "1152x648")
        cmd += ["--", "--uzu-capture", stage]
        with open(os.path.join(out_dir, prefix + "_run.log"), "w", encoding="utf-8") as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=1200, env=env)
        ok = True
        for name in ["title", "hud", "dialogue"]:
            src = os.path.join(stage, name + ".png")
            dst = os.path.join(out_dir, "%s_%s.png" % (prefix, name))
            if os.path.exists(src):
                shutil.copyfile(src, dst)
                print("wrote", dst)
            else:
                ok = False
                print("MISSING", dst)
        return 0 if ok else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
