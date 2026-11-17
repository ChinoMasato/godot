/**************************************************************************/
/*  test_uzu_post_effect.cpp                                              */
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

TEST_FORCE_LINK(test_uzu_post_effect)

#include "core/object/class_db.h"
#include "scene/resources/uzu_post_effect.h"

namespace TestUZUPostEffect {

TEST_CASE("[UZUPostEffect] Defaults, ranges and registration") {
	Ref<UZUPostEffect> effect;
	effect.instantiate();
	CHECK(ClassDB::class_exists("UZUPostEffect"));
	CHECK(ClassDB::is_parent_class("UZUPostEffect", "CompositorEffect"));
	CHECK(effect->get_enabled());
	// Runs after transparent geometry, on the final HDR color.
	CHECK(effect->get_effect_callback_type() == CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
	CHECK(effect->get_edge_strength() == doctest::Approx(0.3));
	CHECK(effect->get_vignette_strength() == doctest::Approx(0.35));

	effect->set_edge_strength(1.2);
	CHECK(effect->get_edge_strength() == doctest::Approx(1.2));
	effect->set_edge_strength(-1.0);
	CHECK(effect->get_edge_strength() == doctest::Approx(0.0));
	effect->set_vignette_strength(2.0);
	CHECK(effect->get_vignette_strength() == doctest::Approx(1.0));
	effect->set_vignette_strength(-0.5);
	CHECK(effect->get_vignette_strength() == doctest::Approx(0.0));
}

} // namespace TestUZUPostEffect
