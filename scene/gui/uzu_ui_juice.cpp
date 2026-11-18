/**************************************************************************/
/*  uzu_ui_juice.cpp                                                      */
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

#include "uzu_ui_juice.h"

#include "core/math/math_defs.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/vector.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/range.h"
#include "scene/gui/uzu_ui.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

// The per-frame driver: created once by ensure_runtime() as a child of the
// scene tree root, so it survives scene changes. It processes always, so UI
// motion also runs while the tree is paused (menus over a paused game).
class UZUUIJuiceRuntime : public Node {
	GDCLASS(UZUUIJuiceRuntime, Node);

public:
	UZUUIJuiceRuntime() {
		set_process_mode(Node::PROCESS_MODE_ALWAYS);
		set_process(true);
		set_name("UZUUIJuiceRuntime");
	}
	uint64_t last_msec = 0;
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_PROCESS) {
			Time *time = Time::get_singleton();
			uint64_t now = time ? time->get_ticks_msec() : 0;
			double dt = last_msec ? double(now - last_msec) / 1000.0 : 0.0;
			last_msec = now;
			UZUUIJuice::apply_animations(dt);
		}
	}
};

void UZUUIJuice::ensure_runtime() {
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Window *root = tree->get_root();
	if (!root) {
		return;
	}
	if (root->get_node_or_null(NodePath("UZUUIJuiceRuntime"))) {
		return;
	}
	root->add_child(memnew(UZUUIJuiceRuntime));
}

// ---------------------------------------------------------------------------
// The curves (pure math).

static double _ease_out_cubic(double p_t) {
	double u = 1.0 - p_t;
	return 1.0 - u * u * u;
}

static double _back_out(double p_t, double p_s) {
	double u = p_t - 1.0;
	return 1.0 + (p_s + 1.0) * u * u * u + p_s * u * u;
}

double UZUUIJuice::overshoot_value(double p_t, int p_level) {
	double s = p_level <= 1 ? 0.8 : (p_level >= 3 ? 2.8 : 1.70158);
	return _back_out(CLAMP(p_t, 0.0, 1.0), s);
}

double UZUUIJuice::motion_value(const String &p_preset, double p_t, double p_strength) {
	double t = CLAMP(p_t, 0.0, 1.0);
	double strength = CLAMP(p_strength, 0.0, 3.0);
	if (p_preset == "overshoot") {
		return _back_out(t, 1.70158 * strength);
	}
	if (p_preset == "ease_in") {
		// The article's slow-in: fast at first, braking into the target like
		// a machine -- a cubic ease-out, with no overshoot at all.
		return _ease_out_cubic(t);
	}
	if (p_preset == "bounce") {
		// The classic ease-out bounce (four arcs of a falling ball).
		const double n1 = 7.5625, d1 = 2.75;
		if (t < 1.0 / d1) {
			return n1 * t * t;
		}
		if (t < 2.0 / d1) {
			double u = t - 1.5 / d1;
			return n1 * u * u + 0.75;
		}
		if (t < 2.5 / d1) {
			double u = t - 2.25 / d1;
			return n1 * u * u + 0.9375;
		}
		double u = t - 2.625 / d1;
		return n1 * u * u + 0.984375;
	}
	if (p_preset == "anticipation") {
		// First a small move the other way (a breath in), then the main move.
		const double phase = 0.3;
		if (t < phase) {
			double u = t / phase;
			return -0.18 * strength * Math::sin(Math::PI * u);
		}
		return _ease_out_cubic((t - phase) / (1.0 - phase));
	}
	if (p_preset == "step") {
		// Frame-by-frame motion for the pixel world: 4 held frames.
		return t >= 1.0 ? 1.0 : Math::floor(t * 4.0) / 4.0;
	}
	return t;
}

PackedStringArray UZUUIJuice::get_preset_names() {
	PackedStringArray names;
	names.push_back("anticipation");
	names.push_back("overshoot");
	names.push_back("ease_in");
	names.push_back("bounce");
	names.push_back("step");
	return names;
}

Dictionary UZUUIJuice::get_preset(const String &p_preset) {
	Dictionary d;
	if (p_preset == "anticipation") {
		d["duration"] = 0.28;
	} else if (p_preset == "overshoot") {
		d["duration"] = 0.25;
		Array levels;
		levels.push_back(0.8);
		levels.push_back(1.70158);
		levels.push_back(2.8);
		d["overshoot_by_level"] = levels;
	} else if (p_preset == "ease_in") {
		d["duration"] = 0.3;
	} else if (p_preset == "bounce") {
		d["duration"] = 0.45;
	} else if (p_preset == "step") {
		d["duration"] = 0.2;
		d["steps"] = 4;
	} else {
		return d;
	}
	d["name"] = p_preset;
	d["strength"] = 1.0;
	return d;
}

// ---------------------------------------------------------------------------
// Running animations. Each entry captures what it found (scale, pivot,
// modulate) and restores it exactly when it ends or is cancelled.

enum UZUUIJuiceKind {
	UI_OPEN,
	UI_CLOSE,
	UI_HOVER,
	UI_COUNT,
	UI_TRANSITION,
};

struct UZUUIJuiceEntry {
	ObjectID id;
	UZUUIJuiceKind kind = UI_OPEN;
	String preset = "overshoot";
	double strength = 1.0;
	double duration = 0.25;
	double elapsed = 0.0;
	int steps = 0; // >0: quantize time into this many held frames.
	Vector2 base_scale = Vector2(1, 1);
	Vector2 base_pivot;
	Color base_modulate = Color(1, 1, 1, 1);
	int64_t count_from = 0;
	int64_t count_to = 0;
	ObjectID partner_id; // TRANSITION: the control fading in.
	Vector2 partner_base_scale = Vector2(1, 1);
	Color partner_base_modulate = Color(1, 1, 1, 1);
};

static Vector<UZUUIJuiceEntry> _entries;
static Vector<Dictionary> _event_log;

static Control *_control_or_null(ObjectID p_id) {
	return Object::cast_to<Control>(ObjectDB::get_instance(p_id));
}

static void _log_event(const String &p_event, const String &p_phase, const String &p_preset, ObjectID p_id) {
	Control *c = _control_or_null(p_id);
	Dictionary e;
	e["event"] = p_event;
	e["phase"] = p_phase;
	e["preset"] = p_preset;
	e["target"] = c ? String(c->get_name()) : String();
	_event_log.push_back(e);
	if (_event_log.size() > 256) {
		_event_log.remove_at(0);
	}
}

static const char *_event_name(UZUUIJuiceKind p_kind) {
	switch (p_kind) {
		case UI_OPEN:
			return "open";
		case UI_CLOSE:
			return "close";
		case UI_HOVER:
			return "hover";
		case UI_COUNT:
			return "count_up";
		case UI_TRANSITION:
			return "transition";
	}
	return "unknown";
}

// Puts the entry's control (and partner) back to the captured state. With
// p_finish, the end state of the animation itself is applied on top:
// a close and a transition-out hide, a count lands on its final value.
static void _restore(const UZUUIJuiceEntry &p_entry, bool p_finish) {
	if (Control *c = _control_or_null(p_entry.id)) {
		c->set_scale(p_entry.base_scale);
		c->set_pivot_offset(p_entry.base_pivot);
		c->set_modulate(p_entry.base_modulate);
		if (p_finish && p_entry.kind == UI_COUNT) {
			if (Label *label = Object::cast_to<Label>(c)) {
				label->set_text(String::num_int64(p_entry.count_to));
			} else if (Range *range = Object::cast_to<Range>(c)) {
				range->set_value(double(p_entry.count_to));
			}
		}
		if (p_finish && (p_entry.kind == UI_CLOSE || p_entry.kind == UI_TRANSITION)) {
			c->set_visible(false);
		}
	}
	if (p_entry.kind == UI_TRANSITION) {
		if (Control *p = _control_or_null(p_entry.partner_id)) {
			p->set_scale(p_entry.partner_base_scale);
			p->set_modulate(p_entry.partner_base_modulate);
		}
	}
}

static void _apply_entry(const UZUUIJuiceEntry &p_entry) {
	double t = p_entry.duration > 0.0 ? CLAMP(p_entry.elapsed / p_entry.duration, 0.0, 1.0) : 1.0;
	if (p_entry.steps > 0 && t < 1.0) {
		t = Math::floor(t * double(p_entry.steps)) / double(p_entry.steps);
	}
	double v = UZUUIJuice::motion_value(p_entry.preset, t, p_entry.strength);
	Control *c = _control_or_null(p_entry.id);
	if (!c) {
		return;
	}
	switch (p_entry.kind) {
		case UI_OPEN: {
			double f = MAX(v, 0.02);
			c->set_scale(p_entry.base_scale * f);
		} break;
		case UI_CLOSE: {
			double f = MAX(1.0 - v, 0.0);
			c->set_scale(p_entry.base_scale * f);
		} break;
		case UI_HOVER: {
			double f = 1.0 + 0.10 * p_entry.strength * Math::sin(Math::PI * CLAMP(p_entry.elapsed / MAX(p_entry.duration, 0.0001), 0.0, 1.0));
			c->set_scale(p_entry.base_scale * f);
		} break;
		case UI_COUNT: {
			double num = double(p_entry.count_from) + double(p_entry.count_to - p_entry.count_from) * v;
			int64_t shown = int64_t(Math::round(num));
			if (Label *label = Object::cast_to<Label>(c)) {
				label->set_text(String::num_int64(shown));
			} else if (Range *range = Object::cast_to<Range>(c)) {
				range->set_value(double(shown));
			}
		} break;
		case UI_TRANSITION: {
			double f = CLAMP(v, 0.0, 1.0);
			Color out = p_entry.base_modulate;
			out.a = p_entry.base_modulate.a * float(1.0 - f);
			c->set_modulate(out);
			if (Control *p = _control_or_null(p_entry.partner_id)) {
				Color in = p_entry.partner_base_modulate;
				in.a = p_entry.partner_base_modulate.a * float(f);
				p->set_modulate(in);
				p->set_scale(p_entry.partner_base_scale * (0.98 + 0.02 * f));
			}
		} break;
	}
}

static void _start(UZUUIJuiceKind p_kind, Control *p_control, const String &p_preset, double p_duration, double p_strength, int p_steps, Control *p_partner = nullptr, int64_t p_from = 0, int64_t p_to = 0) {
	ERR_FAIL_NULL(p_control);
	// One animation per control: restore the old one before capturing anew.
	for (int i = _entries.size() - 1; i >= 0; i--) {
		if (_entries[i].id == p_control->get_instance_id()) {
			_restore(_entries[i], false);
			_entries.remove_at(i);
		}
	}
	UZUUIJuiceEntry entry;
	entry.id = p_control->get_instance_id();
	entry.kind = p_kind;
	entry.preset = UZUUIJuice::get_preset(p_preset).is_empty() ? "overshoot" : p_preset;
	entry.strength = CLAMP(p_strength, 0.0, 3.0);
	entry.duration = CLAMP(p_duration, 0.01, 10.0);
	entry.steps = p_steps;
	entry.base_scale = p_control->get_scale();
	entry.base_pivot = p_control->get_pivot_offset();
	entry.base_modulate = p_control->get_modulate();
	entry.count_from = p_from;
	entry.count_to = p_to;
	if (p_control->get_size().x > 0.0f && p_control->get_size().y > 0.0f) {
		p_control->set_pivot_offset(p_control->get_size() * 0.5f);
	}
	if (p_kind == UI_OPEN || p_kind == UI_TRANSITION) {
		p_control->set_visible(true);
	}
	if (p_kind == UI_TRANSITION && p_partner) {
		entry.partner_id = p_partner->get_instance_id();
		entry.partner_base_scale = p_partner->get_scale();
		entry.partner_base_modulate = p_partner->get_modulate();
		p_partner->set_visible(true);
	}
	_entries.push_back(entry);
	_apply_entry(entry);
	_log_event(_event_name(p_kind), "start", entry.preset, entry.id);
	UZUUIJuice::ensure_runtime();
}

void UZUUIJuice::open_panel(Control *p_control, const String &p_preset, double p_duration, double p_strength) {
	_start(UI_OPEN, p_control, p_preset, p_duration, p_strength, 0);
}

void UZUUIJuice::close_panel(Control *p_control, const String &p_preset, double p_duration, double p_strength) {
	_start(UI_CLOSE, p_control, p_preset, p_duration, p_strength, 0);
}

void UZUUIJuice::hover_pulse(Control *p_control, double p_strength, double p_duration) {
	_start(UI_HOVER, p_control, "overshoot", p_duration, p_strength, 0);
}

void UZUUIJuice::count_up(Control *p_control, int64_t p_from, int64_t p_to, double p_duration, const String &p_preset, double p_strength) {
	_start(UI_COUNT, p_control, p_preset, p_duration, p_strength, 0, nullptr, p_from, p_to);
}

bool UZUUIJuice::transition(Control *p_from, Control *p_to, double p_duration, const String &p_preset, double p_strength) {
	ERR_FAIL_NULL_V(p_from, false);
	ERR_FAIL_NULL_V(p_to, false);
	_start(UI_TRANSITION, p_from, p_preset, p_duration, p_strength, 0, p_to);
	return true;
}

bool UZUUIJuice::apply_world_motion(Control *p_control, const String &p_world, bool p_open) {
	ERR_FAIL_NULL_V(p_control, false);
	if (!UZUUI::get_world_names().has(p_world)) {
		return false;
	}
	Dictionary m = UZUUI::get_world_motion(p_world);
	double duration = double(m["duration"]);
	double overshoot = double(m["overshoot"]);
	int steps = int(m["steps"]);
	String preset = "ease_in";
	double strength = 1.0;
	if (steps > 0) {
		preset = "step";
	} else if (overshoot > 0.01) {
		preset = "overshoot";
		strength = overshoot / 1.70158; // The world's value is the back constant itself.
	}
	_start(p_open ? UI_OPEN : UI_CLOSE, p_control, preset, duration, strength, steps);
	return true;
}

bool UZUUIJuice::is_animating(Control *p_control) {
	ERR_FAIL_NULL_V(p_control, false);
	for (const UZUUIJuiceEntry &e : _entries) {
		if (e.id == p_control->get_instance_id() || e.partner_id == p_control->get_instance_id()) {
			return true;
		}
	}
	return false;
}

int UZUUIJuice::get_active_animation_count() {
	return _entries.size();
}

void UZUUIJuice::cancel(Control *p_control) {
	ERR_FAIL_NULL(p_control);
	for (int i = _entries.size() - 1; i >= 0; i--) {
		if (_entries[i].id == p_control->get_instance_id()) {
			_restore(_entries[i], false);
			_entries.remove_at(i);
		}
	}
}

void UZUUIJuice::cancel_all() {
	for (int i = _entries.size() - 1; i >= 0; i--) {
		_restore(_entries[i], false);
	}
	_entries.clear();
}

Array UZUUIJuice::get_event_log() {
	Array out;
	for (const Dictionary &e : _event_log) {
		out.push_back(e);
	}
	return out;
}

void UZUUIJuice::clear_event_log() {
	_event_log.clear();
}

void UZUUIJuice::apply_animations(double p_dt) {
	double dt = MAX(p_dt, 0.0);
	for (int i = _entries.size() - 1; i >= 0; i--) {
		UZUUIJuiceEntry &e = _entries.write[i];
		if (!_control_or_null(e.id)) {
			_entries.remove_at(i);
			continue;
		}
		e.elapsed += dt;
		if (e.elapsed >= e.duration) {
			UZUUIJuiceEntry done = e;
			_entries.remove_at(i);
			_restore(done, true);
			_log_event(_event_name(done.kind), "end", done.preset, done.id);
		} else {
			_apply_entry(e);
		}
	}
}

void UZUUIJuice::_bind_methods() {
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("get_preset_names"), &UZUUIJuice::get_preset_names);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("get_preset", "preset"), &UZUUIJuice::get_preset);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("motion_value", "preset", "t", "strength"), &UZUUIJuice::motion_value, DEFVAL(1.0));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("overshoot_value", "t", "level"), &UZUUIJuice::overshoot_value, DEFVAL(2));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("open_panel", "control", "preset", "duration", "strength"), &UZUUIJuice::open_panel, DEFVAL("overshoot"), DEFVAL(0.25), DEFVAL(1.0));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("close_panel", "control", "preset", "duration", "strength"), &UZUUIJuice::close_panel, DEFVAL("anticipation"), DEFVAL(0.22), DEFVAL(1.0));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("hover_pulse", "control", "strength", "duration"), &UZUUIJuice::hover_pulse, DEFVAL(1.0), DEFVAL(0.18));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("count_up", "control", "from", "to", "duration", "preset", "strength"), &UZUUIJuice::count_up, DEFVAL(0.6), DEFVAL("ease_in"), DEFVAL(1.0));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("transition", "from_control", "to_control", "duration", "preset", "strength"), &UZUUIJuice::transition, DEFVAL(0.3), DEFVAL("ease_in"), DEFVAL(1.0));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("apply_world_motion", "control", "world", "open"), &UZUUIJuice::apply_world_motion, DEFVAL(true));
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("is_animating", "control"), &UZUUIJuice::is_animating);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("get_active_animation_count"), &UZUUIJuice::get_active_animation_count);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("cancel", "control"), &UZUUIJuice::cancel);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("cancel_all"), &UZUUIJuice::cancel_all);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("get_event_log"), &UZUUIJuice::get_event_log);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("clear_event_log"), &UZUUIJuice::clear_event_log);
	ClassDB::bind_static_method("UZUUIJuice", D_METHOD("apply_animations", "dt"), &UZUUIJuice::apply_animations);
}
