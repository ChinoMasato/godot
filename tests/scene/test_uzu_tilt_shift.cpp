/**************************************************************************/
/*  test_uzu_tilt_shift.cpp                                               */
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

TEST_FORCE_LINK(test_uzu_tilt_shift)

#include "core/object/class_db.h"
#include "scene/resources/uzu_tilt_shift.h"

namespace TestUZUTiltShift {

TEST_CASE("[UZUTiltShift] Defaults, ranges and registration") {
	CHECK(ClassDB::class_exists("UZUTiltShift"));
	CHECK(ClassDB::is_parent_class("UZUTiltShift", "CompositorEffect"));

	Ref<UZUTiltShift> effect;
	effect.instantiate();
	CHECK(effect->get_focus_center() == doctest::Approx(0.55));
	CHECK(effect->get_focus_width() == doctest::Approx(0.12));
	CHECK(effect->get_transition() == doctest::Approx(0.25));
	CHECK(effect->get_max_blur() == doctest::Approx(6.0));

	effect->set_focus_center(1.5);
	CHECK(effect->get_focus_center() == doctest::Approx(1.0));
	effect->set_focus_width(-1.0);
	CHECK(effect->get_focus_width() == doctest::Approx(0.0));
	effect->set_transition(0.0);
	CHECK(effect->get_transition() > 0.0);
	effect->set_max_blur(100.0);
	CHECK(effect->get_max_blur() == doctest::Approx(24.0));
}

TEST_CASE("[UZUTiltShift] The band is sharp and the blur grows away from it") {
	Ref<UZUTiltShift> effect;
	effect.instantiate();
	effect->set_focus_center(0.5);
	effect->set_focus_width(0.1);
	effect->set_transition(0.2);
	effect->set_max_blur(8.0);

	// Inside the band: sharp.
	CHECK(effect->get_blur_radius_at(0.5) == doctest::Approx(0.0));
	CHECK(effect->get_blur_radius_at(0.6) == doctest::Approx(0.0));
	CHECK(effect->get_blur_radius_at(0.4) == doctest::Approx(0.0));
	// Halfway through the transition: half the blur (smoothstep), the same above and below.
	CHECK(effect->get_blur_radius_at(0.7) == doctest::Approx(4.0));
	CHECK(effect->get_blur_radius_at(0.3) == doctest::Approx(4.0));
	// Beyond the transition: the full blur, at the top and the bottom of the screen.
	CHECK(effect->get_blur_radius_at(0.0) == doctest::Approx(8.0));
	CHECK(effect->get_blur_radius_at(1.0) == doctest::Approx(8.0));
	// Monotonic away from the band.
	CHECK(effect->get_blur_radius_at(0.65) < effect->get_blur_radius_at(0.75));
	// The radius is in pixels at 1080 lines and scales with the screen height.
	CHECK(effect->get_blur_radius_at(1.0, 540.0) == doctest::Approx(4.0));
	CHECK(effect->get_blur_radius_at(1.0, 2160.0) == doctest::Approx(16.0));
	CHECK(effect->get_blur_radius_at(1.0, 8000.0) == doctest::Approx(24.0)); // The shader's limit.
}

} // namespace TestUZUTiltShift
