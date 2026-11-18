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
};
