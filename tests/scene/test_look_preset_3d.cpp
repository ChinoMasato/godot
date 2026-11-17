/**************************************************************************/
/*  test_look_preset_3d.cpp                                               */
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

TEST_FORCE_LINK(test_look_preset_3d)

#ifndef _3D_DISABLED

#include "scene/3d/light_3d.h"
#include "scene/3d/look_preset_3d.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"

namespace TestLookPreset3D {

TEST_CASE("[SceneTree][LookPreset3D] Default quality follows the renderer") {
	CHECK(LookPreset3D::get_default_quality("forward_plus") == LookPreset3D::QUALITY_HIGH);
	CHECK(LookPreset3D::get_default_quality("mobile") == LookPreset3D::QUALITY_STANDARD);
	CHECK(LookPreset3D::get_default_quality("gl_compatibility") == LookPreset3D::QUALITY_LOW);
	CHECK(LookPreset3D::get_default_quality("dummy") == LookPreset3D::QUALITY_LOW);
}

TEST_CASE("[SceneTree][LookPreset3D] Environment is lit by a sky and graded") {
	const LookPreset3D::Quality qualities[] = { LookPreset3D::QUALITY_HIGH, LookPreset3D::QUALITY_STANDARD, LookPreset3D::QUALITY_LOW };
	for (LookPreset3D::Quality quality : qualities) {
		Ref<Environment> environment = LookPreset3D::create_environment(quality);
		REQUIRE(environment.is_valid());

		CHECK(environment->get_background() == Environment::BG_SKY);
		REQUIRE(environment->get_sky().is_valid());
		CHECK(Object::cast_to<ProceduralSkyMaterial>(environment->get_sky()->get_material().ptr()) != nullptr);
		CHECK(environment->get_ambient_source() == Environment::AMBIENT_SOURCE_SKY);
		CHECK(environment->get_ambient_light_energy() == doctest::Approx(0.5));
		CHECK(environment->get_reflection_source() == Environment::REFLECTION_SOURCE_SKY);

		// The class defaults (linear tonemapping, no glow, no fog) are what make a fresh scene look flat.
		CHECK(environment->get_tonemapper() == Environment::TONE_MAPPER_ACES);
		CHECK(environment->get_tonemap_exposure() == doctest::Approx(0.8));
		CHECK(environment->is_glow_enabled());
		CHECK(environment->is_fog_enabled());
		CHECK(environment->get_fog_mode() == Environment::FOG_MODE_EXPONENTIAL);
		CHECK(environment->get_fog_density() == doctest::Approx(0.002));
	}
}

TEST_CASE("[SceneTree][LookPreset3D] Screen-space effects scale with quality") {
	Ref<Environment> high = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH);
	CHECK(high->is_ssao_enabled());
	CHECK(high->is_ssil_enabled());

	Ref<Environment> standard = LookPreset3D::create_environment(LookPreset3D::QUALITY_STANDARD);
	CHECK(standard->is_ssao_enabled());
	CHECK_FALSE(standard->is_ssil_enabled());

	// The Compatibility renderer does not support SSAO or SSIL.
	Ref<Environment> low = LookPreset3D::create_environment(LookPreset3D::QUALITY_LOW);
	CHECK_FALSE(low->is_ssao_enabled());
	CHECK_FALSE(low->is_ssil_enabled());

	// Heavy features stay off at every quality.
	CHECK_FALSE(high->is_sdfgi_enabled());
	CHECK_FALSE(high->is_volumetric_fog_enabled());
}

TEST_CASE("[SceneTree][LookPreset3D] Sun casts split shadows") {
	DirectionalLight3D *sun = memnew(DirectionalLight3D);

	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_HIGH);
	CHECK(sun->has_shadow());
	CHECK(sun->get_shadow_mode() == DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
	CHECK(sun->is_blend_splits_enabled());
	CHECK(sun->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE) == doctest::Approx(150.0));
	CHECK(sun->get_param(Light3D::PARAM_SIZE) == doctest::Approx(0.5));
	CHECK(sun->get_rotation_degrees().is_equal_approx(Vector3(-50.0, 150.0, 0.0)));

	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_STANDARD);
	CHECK(sun->get_shadow_mode() == DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
	CHECK(sun->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE) == doctest::Approx(100.0));

	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_LOW);
	CHECK(sun->has_shadow());
	CHECK(sun->get_shadow_mode() == DirectionalLight3D::SHADOW_PARALLEL_2_SPLITS);
	CHECK(sun->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE) == doctest::Approx(60.0));

	memdelete(sun);
}

TEST_CASE("[SceneTree][LookPreset3D] Class defaults stay unchanged") {
	// Existing scenes do not store default values, so changing them would change how those scenes look.
	Ref<Environment> environment;
	environment.instantiate();
	CHECK(environment->get_tonemapper() == Environment::TONE_MAPPER_LINEAR);
	CHECK_FALSE(environment->is_glow_enabled());
	CHECK_FALSE(environment->is_fog_enabled());
	CHECK_FALSE(environment->is_ssao_enabled());
}

} // namespace TestLookPreset3D

#endif // _3D_DISABLED
