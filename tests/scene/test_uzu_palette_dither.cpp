/**************************************************************************/
/*  test_uzu_palette_dither.cpp                                           */
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

TEST_FORCE_LINK(test_uzu_palette_dither)

#include "core/object/class_db.h"
#include "scene/resources/uzu_palette_dither.h"

namespace TestUZUPaletteDither {

TEST_CASE("[UZUPaletteDither] Defaults, ranges and registration") {
	CHECK(ClassDB::class_exists("UZUPaletteDither"));
	CHECK(ClassDB::is_parent_class("UZUPaletteDither", "CompositorEffect"));
	Ref<UZUPaletteDither> effect;
	effect.instantiate();
	CHECK(effect->get_levels() == 6);
	CHECK(effect->get_dither_strength() == doctest::Approx(1.0));
	CHECK(effect->get_dither_scale() == 1);
	effect->set_levels(1);
	CHECK(effect->get_levels() == 2);
	effect->set_levels(1000);
	CHECK(effect->get_levels() == 64);
	effect->set_dither_strength(3.0);
	CHECK(effect->get_dither_strength() == doctest::Approx(1.0));
	effect->set_dither_scale(0);
	CHECK(effect->get_dither_scale() == 1);
}

TEST_CASE("[UZUPaletteDither] Colors snap to the levels; the Bayer pattern breaks the bands") {
	Ref<UZUPaletteDither> effect;
	effect.instantiate();
	effect->set_levels(4); // Gamma-space values 0, 1/3, 2/3, 1.
	effect->set_dither_strength(0.0);
	const float allowed[4] = { 0.0f, Math::pow(1.0f / 3.0f, 2.2f), Math::pow(2.0f / 3.0f, 2.2f), 1.0f };
	for (float v = 0.0f; v <= 1.0f; v += 0.05f) {
		const Color q = effect->quantize(Color(v, v * 0.5f, 1.0f - v), Vector2i(3, 7));
		for (int i = 0; i < 3; i++) {
			bool on_level = false;
			for (float a : allowed) {
				on_level = on_level || Math::is_equal_approx(q[i], a);
			}
			CHECK(on_level);
		}
	}
	// Alpha is kept.
	CHECK(effect->quantize(Color(0.3, 0.3, 0.3, 0.42), Vector2i()).a == doctest::Approx(0.42));

	// The 4x4 Bayer thresholds: 16 different values in [-0.5, 0.5) that average to 0.
	float sum = 0.0f;
	float lowest = 1.0f;
	float highest = -1.0f;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			const float t = effect->get_dither_threshold(Vector2i(x, y));
			sum += t;
			lowest = MIN(lowest, t);
			highest = MAX(highest, t);
			CHECK(t == doctest::Approx(effect->get_dither_threshold(Vector2i(x + 4, y + 8)))); // Tiles every 4 pixels.
		}
	}
	CHECK(sum == doctest::Approx(0.0));
	CHECK(lowest >= -0.5);
	CHECK(highest < 0.5);

	// With dithering, a flat color between two levels becomes a mix of both, so the
	// average over the pattern stays close to the original (in gamma space).
	effect->set_dither_strength(1.0);
	const float g = 0.5f; // Halfway between 1/3 and 2/3.
	const float original = Math::pow(g, 2.2f);
	float gamma_sum = 0.0f;
	int low = 0;
	int high = 0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			const float q = effect->quantize(Color(original, original, original), Vector2i(x, y)).r;
			gamma_sum += Math::pow(q, 1.0f / 2.2f);
			low += Math::is_equal_approx(q, allowed[1]);
			high += Math::is_equal_approx(q, allowed[2]);
		}
	}
	CHECK(low > 0);
	CHECK(high > 0);
	CHECK(low + high == 16);
	CHECK(gamma_sum / 16.0f == doctest::Approx(g).epsilon(0.05));

	// A bigger dither cell makes the pattern chunkier.
	effect->set_dither_scale(2);
	CHECK(effect->get_dither_threshold(Vector2i(0, 0)) == doctest::Approx(effect->get_dither_threshold(Vector2i(1, 1))));
}

} // namespace TestUZUPaletteDither
