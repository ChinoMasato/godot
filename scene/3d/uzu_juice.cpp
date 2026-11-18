/**************************************************************************/
/*  uzu_juice.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "uzu_juice.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "scene/2d/camera_2d.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/node.h"
#include "scene/main/viewport.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

static uint64_t _now_msec();

// The per-frame driver for UZUJuice's effects: created once by
// UZUJuice::ensure_runtime() as a child of the scene tree root, so it survives
// scene changes. It processes always (even while the tree is paused), because
// its deadlines are wall-clock based and must expire on time.
class UZUJuiceRuntime : public Node {
	GDCLASS(UZUJuiceRuntime, Node);

public:
	UZUJuiceRuntime() {
		set_process_mode(Node::PROCESS_MODE_ALWAYS);
		set_process(true);
		set_name("UZUJuiceRuntime");
	}
	uint64_t last_msec = 0;
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_PROCESS) {
			// Deadlines use the wall clock, not the scaled delta.
			UZUJuice::apply_time_state();
			uint64_t now = _now_msec();
			double dt = last_msec ? double(now - last_msec) / 1000.0 : 0.0;
			last_msec = now;
			UZUJuice::apply_shake(dt, double(now) / 1000.0);
		}
	}
};

static uint64_t _now_msec() {
	Time *time = Time::get_singleton();
	return time ? time->get_ticks_msec() : 0;
}

static double _engine_scale() {
	Engine *engine = Engine::get_singleton();
	return engine ? engine->get_time_scale() : 1.0;
}

UZUJuiceTimeState &UZUJuice::time_state() {
	static UZUJuiceTimeState state;
	return state;
}

void UZUJuice::apply_time_state() {
	Engine *engine = Engine::get_singleton();
	if (!engine) {
		return;
	}
	double effective = time_state().effective_scale(_now_msec());
	if (!Math::is_equal_approx(effective, engine->get_time_scale())) {
		engine->set_time_scale(effective);
	}
}

void UZUJuice::ensure_runtime() {
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Window *root = tree->get_root();
	if (!root) {
		return;
	}
	if (root->get_node_or_null(NodePath("UZUJuiceRuntime"))) {
		return;
	}
	UZUJuiceRuntime *runtime = memnew(UZUJuiceRuntime);
	root->add_child(runtime);
}

void UZUJuice::hitstop(double p_duration, double p_scale) {
	double duration = CLAMP(p_duration, 0.0, 60.0);
	double scale = CLAMP(p_scale, 0.0, 4.0);
	time_state().start_hitstop(_now_msec(), duration, scale, _engine_scale());
	ensure_runtime();
	apply_time_state();
}

void UZUJuice::slowmo(double p_scale, double p_duration) {
	double scale = CLAMP(p_scale, 0.0, 4.0);
	double duration = CLAMP(p_duration, 0.0, 60.0);
	time_state().start_slowmo(_now_msec(), scale, duration, _engine_scale());
	ensure_runtime();
	apply_time_state();
}

void UZUJuice::cancel_time_effects() {
	time_state().cancel();
	apply_time_state();
}

bool UZUJuice::is_time_effect_active() {
	return time_state().active(_now_msec());
}

double UZUJuice::applied_time_scale() {
	return time_state().effective_scale(_now_msec());
}

UZUJuiceShakeState &UZUJuice::shake_state() {
	static UZUJuiceShakeState state;
	return state;
}

void UZUJuice::shake(double p_amount) {
	shake_state().add(p_amount);
	ensure_runtime();
}

void UZUJuice::clear_shake() {
	shake_state().clear();
	apply_shake(0.0, double(_now_msec()) / 1000.0);
}

double UZUJuice::get_trauma() {
	return shake_state().trauma;
}

// The camera offsets the shake found when it took over, per camera, so the
// game's own offset is restored exactly when the shake ends.
static ObjectID _shake_cam3d_id;
static Vector2 _shake_cam3d_base;
static ObjectID _shake_cam2d_id;
static Vector2 _shake_cam2d_base;

template <typename T>
static T *_instance_or_null(ObjectID p_id) {
	return Object::cast_to<T>(ObjectDB::get_instance(p_id));
}

void UZUJuice::apply_shake(double p_real_dt, double p_now_sec) {
	UZUJuiceShakeState &state = shake_state();
	state.update(p_real_dt);
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Window *root = tree->get_root();
	if (!root) {
		return;
	}
	Viewport *viewport = root;
	Vector2 offset = state.offset_at(p_now_sec, 0.5);
	bool shaking = state.trauma > 0.0;

	Camera3D *cam3 = viewport->get_camera_3d();
	if (cam3) {
		if (_shake_cam3d_id != cam3->get_instance_id()) {
			if (Camera3D *old = _instance_or_null<Camera3D>(_shake_cam3d_id)) {
				old->set_h_offset(_shake_cam3d_base.x);
				old->set_v_offset(_shake_cam3d_base.y);
			}
			_shake_cam3d_id = cam3->get_instance_id();
			_shake_cam3d_base = Vector2(cam3->get_h_offset(), cam3->get_v_offset());
		}
		Vector2 target = shaking ? _shake_cam3d_base + offset : _shake_cam3d_base;
		cam3->set_h_offset(target.x);
		cam3->set_v_offset(target.y);
		if (!shaking) {
			_shake_cam3d_id = ObjectID();
		}
	}

	Camera2D *cam2 = viewport->get_camera_2d();
	if (cam2) {
		if (_shake_cam2d_id != cam2->get_instance_id()) {
			if (Camera2D *old = _instance_or_null<Camera2D>(_shake_cam2d_id)) {
				old->set_offset(_shake_cam2d_base);
			}
			_shake_cam2d_id = cam2->get_instance_id();
			_shake_cam2d_base = cam2->get_offset();
		}
		cam2->set_offset(shaking ? _shake_cam2d_base + offset * 64.0 : _shake_cam2d_base);
		if (!shaking) {
			_shake_cam2d_id = ObjectID();
		}
	}
}

void UZUJuice::_bind_methods() {
	ClassDB::bind_static_method("UZUJuice", D_METHOD("hitstop", "duration", "scale"), &UZUJuice::hitstop, DEFVAL(0.0));
	ClassDB::bind_static_method("UZUJuice", D_METHOD("slowmo", "scale", "duration"), &UZUJuice::slowmo);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("cancel_time_effects"), &UZUJuice::cancel_time_effects);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("is_time_effect_active"), &UZUJuice::is_time_effect_active);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("applied_time_scale"), &UZUJuice::applied_time_scale);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("shake", "amount"), &UZUJuice::shake);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("clear_shake"), &UZUJuice::clear_shake);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("get_trauma"), &UZUJuice::get_trauma);
}
