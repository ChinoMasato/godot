/**************************************************************************/
/*  uzu_juice.h                                                           */
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

#pragma once

#include "core/math/math_funcs.h"
#include "core/math/color.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/object/object.h"

// U.ZU game feel (BACKLOG Phase 11, UZUJuice): the small effects that make a game
// feel good -- hitstop, slow motion, screen shake, flashes -- behind one static
// call, with the sound side provided by UZUSfx (B-1105) through the event
// presets (B-1106). B-1101 is the foundation: a runtime node that UZUJuice
// creates in the scene tree on first use, plus the time effects.
//
// Time effects are decided by a pure state value (UZUJuiceTimeState) against
// wall-clock milliseconds, so they are unit-testable without a scene tree and
// immune to the time scale they themselves set. While an effect is active,
// UZUJuice owns Engine.time_scale; when the last effect ends, the scale that
// was in place before the first effect started is restored.
struct UZUJuiceTimeState {
	uint64_t hitstop_until_msec = 0;
	double hitstop_scale = 0.0;
	uint64_t slowmo_until_msec = 0;
	double slowmo_scale = 1.0;
	double base_scale = 1.0;
	bool base_captured = false;

	// The scale in effect at p_now_msec: a running hitstop wins over a running
	// slow motion; with nothing running, the captured base scale.
	double effective_scale(uint64_t p_now_msec) const {
		if (p_now_msec < hitstop_until_msec) {
			return hitstop_scale;
		}
		if (p_now_msec < slowmo_until_msec) {
			return slowmo_scale;
		}
		return base_scale;
	}
	bool active(uint64_t p_now_msec) const {
		return p_now_msec < hitstop_until_msec || p_now_msec < slowmo_until_msec;
	}
	void start_hitstop(uint64_t p_now_msec, double p_duration_sec, double p_scale, double p_engine_scale) {
		if (!active(p_now_msec)) {
			base_scale = p_engine_scale;
			base_captured = true;
		}
		hitstop_scale = p_scale;
		hitstop_until_msec = p_now_msec + uint64_t(p_duration_sec * 1000.0);
	}
	void start_slowmo(uint64_t p_now_msec, double p_scale, double p_duration_sec, double p_engine_scale) {
		if (!active(p_now_msec)) {
			base_scale = p_engine_scale;
			base_captured = true;
		}
		slowmo_scale = p_scale;
		slowmo_until_msec = p_now_msec + uint64_t(p_duration_sec * 1000.0);
	}
	void cancel() {
		hitstop_until_msec = 0;
		slowmo_until_msec = 0;
	}
};

// Screen shake state (BACKLOG B-1102): "trauma" rises with impacts and decays
// linearly; the applied offset scales with trauma squared, so small shakes
// stay subtle and big ones saturate. The offset itself is a deterministic
// pseudo-noise of time, so it is unit-testable without a camera.
struct UZUJuiceShakeState {
	double trauma = 0.0;

	void add(double p_amount) { trauma = CLAMP(trauma + p_amount, 0.0, 1.0); }
	void update(double p_dt) { trauma = MAX(0.0, trauma - 1.6 * MAX(p_dt, 0.0)); }
	void clear() { trauma = 0.0; }
	double strength() const { return trauma * trauma; }
	Vector2 offset_at(double p_t, double p_max_offset) const {
		double s = strength() * p_max_offset;
		double nx = Math::sin(p_t * 41.0) * 0.6 + Math::sin(p_t * 83.0 + 1.7) * 0.4;
		double ny = Math::cos(p_t * 53.0 + 0.6) * 0.6 + Math::sin(p_t * 97.0 + 4.1) * 0.4;
		return Vector2(nx * s, ny * s);
	}
};

// Spring state for squash & stretch (BACKLOG B-1103): a slightly underdamped
// spring; give it a displacement and it overshoots back through zero and
// settles. Pure math, unit-testable without a node.
struct UZUJuiceSpringState {
	double x = 0.0; // Displacement: + stretches wide, - squashes flat (by convention of the applier).
	double v = 0.0;
	void displace(double p_amount) { x = p_amount; }
	void update(double p_dt) {
		// Semi-implicit Euler; stiffness 180, damping 14 (underdamped).
		double a = -180.0 * x - 14.0 * v;
		v += a * p_dt;
		x += v * p_dt;
	}
	bool settled() const { return Math::abs(x) < 0.001 && Math::abs(v) < 0.01; }
};

// Camera kick state (BACKLOG B-1104): a spring punch of the camera's zoom --
// the field of view dips (3D) or the zoom rises (2D) and springs back.
struct UZUJuiceKickState {
	UZUJuiceSpringState spring;
	void punch(double p_amount) { spring.displace(p_amount); }
	void update(double p_dt) { spring.update(p_dt); }
	bool settled() const { return spring.settled(); }
	double fov_delta() const { return -spring.x * 8.0; } // Degrees: negative punches in.
	double zoom_factor() const { return 1.0 + spring.x * 0.25; }
};

class CPUParticles2D;
class CPUParticles3D;
class Node;

class UZUJuice : public Object {
	GDCLASS(UZUJuice, Object);

protected:
	static void _bind_methods();

public:
	// Freezes (or nearly freezes) the game's time for p_duration real seconds,
	// at p_scale (0 = full stop). The classic hit-confirmation pause.
	static void hitstop(double p_duration, double p_scale = 0.0);
	// Runs the game's time at p_scale for p_duration real seconds, then
	// restores the previous scale.
	static void slowmo(double p_scale, double p_duration);
	// Ends all time effects now, restoring the scale from before they started.
	static void cancel_time_effects();
	// True while a hitstop or slow motion is still running.
	static bool is_time_effect_active();
	// The time scale UZUJuice is applying right now (the base scale when idle).
	static double applied_time_scale();

	// The shared state, exposed for the runtime node and the unit tests.
	static UZUJuiceTimeState &time_state();
	// Recomputes the effective scale from the wall clock and applies it to the
	// Engine; called on every effect start and by the runtime node each frame.
	static void apply_time_state();
	// Creates the runtime node in the scene tree if there is none yet. Safe to
	// call without a running scene tree (it simply does nothing then).
	static void ensure_runtime();

	// Adds screen shake: p_amount of trauma (0..1; a normal hit is about 0.3,
	// a big explosion 0.7). Trauma decays on its own; the offset it produces
	// scales with trauma squared.
	static void shake(double p_amount);
	// Stops the shake now, restoring the camera exactly.
	static void clear_shake();
	// The current trauma (0..1).
	static double get_trauma();

	// The shared shake state, exposed for the runtime node and the unit tests.
	static UZUJuiceShakeState &shake_state();
	// Decays the shake by p_real_dt seconds and applies the offset for time
	// p_now_sec to the current camera (Camera3D h/v offset, Camera2D offset),
	// capturing the camera's own offset first and restoring it when the shake
	// ends. Called by the runtime node each frame.
	static void apply_shake(double p_real_dt, double p_now_sec);

	// Tints p_node toward p_color for p_duration seconds, then restores it
	// exactly. Works on CanvasItem and Sprite3D (their modulate) and on other
	// GeometryInstance3D (a fading material overlay).
	static void flash(Node *p_node, const Color &p_color = Color(1, 1, 1), double p_duration = 0.08);
	// Squashes p_node (Node2D or Node3D) by p_amount (0.3 is a firm landing)
	// and lets a spring bring it back with a wobble, roughly preserving
	// volume. The node's own scale is captured and restored exactly.
	static void squash(Node *p_node, double p_amount = 0.3);

	// Advances the node effects (flashes and springs) by p_real_dt seconds.
	// Called by the runtime node each frame.
	static void apply_node_effects(double p_real_dt);

	// A one-shot burst of p_count particles at p_global_position under
	// p_parent, in p_color, flying outward and falling; the node frees itself
	// when the burst is over. p_power scales speed and lifetime.
	static CPUParticles3D *burst3d(Node *p_parent, const Vector3 &p_global_position, const Color &p_color, int p_count = 14, double p_power = 1.0);
	// The 2D form of burst3d, under a Node2D parent at a global position.
	static CPUParticles2D *burst2d(Node *p_parent, const Vector2 &p_global_position, const Color &p_color, int p_count = 14, double p_power = 1.0);
	// Punches the current camera's zoom: the field of view dips by up to
	// 8 * p_amount degrees (2D: the zoom rises), springing back on its own.
	static void kick(double p_amount = 0.5);
	// The current kick displacement (0 when settled).
	static double get_kick();

	// Event presets (B-1106): one call that fires the right combination of
	// parts plus a synthesized sound: "hit", "kill", "land", "pickup",
	// "damage". Returns false for an unknown kind. p_power scales the
	// violent parts (shake, bursts, sound volume stays constant).
	static bool impact(Node *p_node, const String &p_kind, double p_power = 1.0);
	static PackedStringArray get_impact_preset_names();

	// The shared kick state, exposed for the runtime and the unit tests.
	static UZUJuiceKickState &kick_state();
};
