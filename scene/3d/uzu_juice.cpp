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
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

// The per-frame driver for UZUJuice's effects: created once by
// UZUJuice::ensure_runtime() as a child of the scene tree root, so it survives
// scene changes. It processes always (even while the tree is paused), because
// its deadlines are wall-clock based and must expire on time.
class UZUJuiceRuntime : public Node {
	GDCLASS(UZUJuiceRuntime, Node);

public:
	UZUJuiceRuntime() {
		set_process_mode(Node::PROCESS_MODE_ALWAYS);
		set_name("UZUJuiceRuntime");
	}
	virtual void _process(double p_delta) override {
		(void)p_delta; // Deadlines use the wall clock, not the scaled delta.
		UZUJuice::apply_time_state();
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
	if (!tree || !tree->get_root()) {
		return;
	}
	if (tree->get_root()->get_node_or_null("UZUJuiceRuntime")) {
		return;
	}
	UZUJuiceRuntime *runtime = memnew(UZUJuiceRuntime);
	tree->get_root()->add_child(runtime);
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

void UZUJuice::_bind_methods() {
	ClassDB::bind_static_method("UZUJuice", D_METHOD("hitstop", "duration", "scale"), &UZUJuice::hitstop, DEFVAL(0.0));
	ClassDB::bind_static_method("UZUJuice", D_METHOD("slowmo", "scale", "duration"), &UZUJuice::slowmo);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("cancel_time_effects"), &UZUJuice::cancel_time_effects);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("is_time_effect_active"), &UZUJuice::is_time_effect_active);
	ClassDB::bind_static_method("UZUJuice", D_METHOD("applied_time_scale"), &UZUJuice::applied_time_scale);
}
