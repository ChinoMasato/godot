/**************************************************************************/
/*  test_uzu_juice.cpp                                                    */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_uzu_juice)

#include "scene/3d/uzu_juice.h"

namespace TestUZUJuice {

TEST_CASE("[Scene][UZUJuice] Time state: idle is the base scale and inactive") {
	UZUJuiceTimeState state;
	CHECK(state.effective_scale(1000) == doctest::Approx(1.0));
	CHECK_FALSE(state.active(1000));
}

TEST_CASE("[Scene][UZUJuice] Time state: hitstop runs for its duration, then restores") {
	UZUJuiceTimeState state;
	state.start_hitstop(1000, 0.06, 0.0, 1.0);
	CHECK(state.active(1000));
	CHECK(state.effective_scale(1000) == doctest::Approx(0.0));
	CHECK(state.effective_scale(1059) == doctest::Approx(0.0));
	CHECK_FALSE(state.active(1060));
	CHECK(state.effective_scale(1060) == doctest::Approx(1.0));
}

TEST_CASE("[Scene][UZUJuice] Time state: hitstop wins over slow motion, slow motion resumes") {
	UZUJuiceTimeState state;
	state.start_slowmo(1000, 0.25, 0.4, 1.0);
	CHECK(state.effective_scale(1100) == doctest::Approx(0.25));
	state.start_hitstop(1100, 0.05, 0.0, 0.25);
	// The base is still the pre-effect scale, not the slow-motion scale.
	CHECK(state.effective_scale(1120) == doctest::Approx(0.0));
	CHECK(state.effective_scale(1200) == doctest::Approx(0.25)); // Hitstop over, slow motion resumes.
	CHECK(state.effective_scale(1500) == doctest::Approx(1.0)); // Both over: the original base.
	CHECK_FALSE(state.active(1500));
}

TEST_CASE("[Scene][UZUJuice] Time state: the base scale is what the game had before") {
	UZUJuiceTimeState state;
	state.start_slowmo(0, 0.5, 0.2, 0.8); // The game itself was running at 0.8.
	CHECK(state.effective_scale(300) == doctest::Approx(0.8));
	state.cancel();
	CHECK_FALSE(state.active(0));
	CHECK(state.effective_scale(0) == doctest::Approx(0.8));
}

TEST_CASE("[Scene][UZUJuice] Static API drives the shared state") {
	UZUJuice::cancel_time_effects();
	CHECK_FALSE(UZUJuice::is_time_effect_active());
	UZUJuice::hitstop(0.05, 0.0);
	CHECK(UZUJuice::is_time_effect_active());
	CHECK(UZUJuice::applied_time_scale() == doctest::Approx(0.0));
	UZUJuice::cancel_time_effects();
	CHECK_FALSE(UZUJuice::is_time_effect_active());
}

} // namespace TestUZUJuice
