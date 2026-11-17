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

#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/look_preset_3d.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
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
		CHECK(Object::cast_to<PhysicalSkyMaterial>(environment->get_sky()->get_material().ptr()) != nullptr);
		CHECK(environment->get_ambient_source() == Environment::AMBIENT_SOURCE_SKY);
		CHECK(environment->get_ambient_light_energy() == doctest::Approx(0.5));
		CHECK(environment->get_reflection_source() == Environment::REFLECTION_SOURCE_SKY);

		// The class defaults (linear tonemapping, no glow, no fog) are what make a fresh scene look flat.
		CHECK(environment->get_tonemapper() == Environment::TONE_MAPPER_ACES);
		CHECK(environment->get_tonemap_exposure() == doctest::Approx(0.8));
		CHECK(environment->is_glow_enabled());
		CHECK(environment->is_fog_enabled());
		CHECK(environment->get_fog_mode() == Environment::FOG_MODE_EXPONENTIAL);
		CHECK(environment->get_fog_density() > 0.0);
	}
}

TEST_CASE("[SceneTree][LookPreset3D] Atmospheres give the two kinds of aerial perspective") {
	const LookPreset3D::Quality quality = LookPreset3D::QUALITY_HIGH;

	// Clear air: small particles scatter blue, so the distance turns blue.
	Ref<Environment> clear = LookPreset3D::create_environment(quality, LookPreset3D::ATMOSPHERE_CLEAR);
	const Color clear_fog = clear->get_fog_light_color();
	CHECK(clear_fog.b > clear_fog.r);
	CHECK(clear->get_fog_aerial_perspective() > 0.5);
	CHECK(clear->get_fog_height_density() == doctest::Approx(0.0));

	// Humid air: larger particles scatter all colors, so the distance turns white, densest near the ground.
	Ref<Environment> humid = LookPreset3D::create_environment(quality, LookPreset3D::ATMOSPHERE_HUMID);
	const Color humid_fog = humid->get_fog_light_color();
	CHECK(humid_fog.get_s() < clear_fog.get_s());
	CHECK(humid_fog.get_v() > clear_fog.get_v());
	CHECK(humid->get_fog_aerial_perspective() < clear->get_fog_aerial_perspective());
	CHECK(humid->get_fog_density() > clear->get_fog_density());
	CHECK(humid->get_fog_height_density() > 0.0);

	Ref<PhysicalSkyMaterial> clear_sky = clear->get_sky()->get_material();
	Ref<PhysicalSkyMaterial> humid_sky = humid->get_sky()->get_material();
	REQUIRE(clear_sky.is_valid());
	REQUIRE(humid_sky.is_valid());
	CHECK(humid_sky->get_mie_coefficient() > clear_sky->get_mie_coefficient());
	CHECK(humid_sky->get_turbidity() > clear_sky->get_turbidity());

	// Golden hour: warm fog.
	Ref<Environment> golden = LookPreset3D::create_environment(quality, LookPreset3D::ATMOSPHERE_GOLDEN_HOUR);
	CHECK(golden->get_fog_light_color().r > golden->get_fog_light_color().b);
}

TEST_CASE("[SceneTree][LookPreset3D] Golden hour has a low, warm sun") {
	DirectionalLight3D *sun = memnew(DirectionalLight3D);

	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_CLEAR);
	const float clear_altitude = -sun->get_rotation_degrees().x;

	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_GOLDEN_HOUR);
	const float golden_altitude = -sun->get_rotation_degrees().x;
	CHECK(golden_altitude > 0.0);
	CHECK(golden_altitude < 20.0);
	CHECK(golden_altitude < clear_altitude);
	CHECK(sun->get_color().r > sun->get_color().b);
	// Shadows keep the quality settings whatever the atmosphere.
	CHECK(sun->get_shadow_mode() == DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);

	memdelete(sun);
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

TEST_CASE("[SceneTree][LookPreset3D] Color grade keeps mid-tones and calms shadows and highlights") {
	// Neutral grey stays close to neutral grey.
	const Color grey = LookPreset3D::grade_color(Color(0.5, 0.5, 0.5));
	CHECK(grey.r == doctest::Approx(0.5).epsilon(0.05));
	CHECK(grey.g == doctest::Approx(0.5).epsilon(0.05));
	CHECK(grey.b == doctest::Approx(0.5).epsilon(0.05));

	// Saturation is lowered a little in the mid-tones, and more in the shadows and highlights.
	const Color mid = Color(0.65, 0.45, 0.3);
	const Color dark = Color(0.2, 0.1, 0.04);
	const Color bright = Color(1.0, 0.9, 0.75);
	const float mid_kept = LookPreset3D::grade_color(mid).get_s() / mid.get_s();
	const float dark_kept = LookPreset3D::grade_color(dark).get_s() / dark.get_s();
	const float bright_kept = LookPreset3D::grade_color(bright).get_s() / bright.get_s();
	CHECK(mid_kept < 1.0);
	CHECK(mid_kept > 0.8);
	CHECK(dark_kept < mid_kept);
	CHECK(bright_kept < mid_kept);

	// Black and white stay in range.
	const Color black = LookPreset3D::grade_color(Color(0, 0, 0));
	const Color white = LookPreset3D::grade_color(Color(1, 1, 1));
	CHECK(black.get_v() < 0.05);
	CHECK(white.get_v() > 0.95);
}

TEST_CASE("[SceneTree][LookPreset3D] Environment uses the color grade LUT") {
	Ref<ImageTexture3D> lut = LookPreset3D::create_color_grade();
	REQUIRE(lut.is_valid());
	CHECK(lut->get_width() == 16);
	CHECK(lut->get_height() == 16);
	CHECK(lut->get_depth() == 16);

	// Texel (r, g, b) holds the grade of the color at its center. This also checks
	// that the texels can be read back without a GPU, so headless saves keep them.
	const Vector<Ref<Image>> slices = lut->get_data();
	REQUIRE(slices.size() == 16);
	const Color expected = LookPreset3D::grade_color(Color(10.5 / 16, 6.5 / 16, 3.5 / 16));
	const Color stored = slices[3]->get_pixel(10, 6);
	CHECK(stored.r == doctest::Approx(expected.r).epsilon(0.01));
	CHECK(stored.g == doctest::Approx(expected.g).epsilon(0.01));
	CHECK(stored.b == doctest::Approx(expected.b).epsilon(0.01));

	Ref<Environment> environment = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH);
	CHECK(environment->is_adjustment_enabled());
	CHECK(Object::cast_to<ImageTexture3D>(environment->get_adjustment_color_correction().ptr()) != nullptr);
	// The other adjustments stay neutral.
	CHECK(environment->get_adjustment_brightness() == doctest::Approx(1.0));
	CHECK(environment->get_adjustment_contrast() == doctest::Approx(1.0));
	CHECK(environment->get_adjustment_saturation() == doctest::Approx(1.0));
}

TEST_CASE("[SceneTree][LookPreset3D] Materials for light that passes through") {
	Ref<StandardMaterial3D> skin = LookPreset3D::create_material(LookPreset3D::MATERIAL_SKIN);
	CHECK(skin->get_feature(BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING));
	CHECK(skin->get_flag(BaseMaterial3D::FLAG_SUBSURFACE_MODE_SKIN));
	CHECK(skin->get_subsurface_scattering_strength() > 0.0);

	Ref<StandardMaterial3D> foliage = LookPreset3D::create_material(LookPreset3D::MATERIAL_FOLIAGE);
	CHECK(foliage->get_feature(BaseMaterial3D::FEATURE_BACKLIGHT));
	CHECK(foliage->get_cull_mode() == BaseMaterial3D::CULL_DISABLED);
	// Light through a leaf is warmer (more yellow) than the leaf itself.
	CHECK(foliage->get_backlight().r > foliage->get_albedo().r);

	Ref<StandardMaterial3D> glass = LookPreset3D::create_material(LookPreset3D::MATERIAL_GLASS);
	CHECK(glass->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA);
	CHECK(glass->get_albedo().a < 0.5);
	CHECK(glass->get_roughness() < 0.1);

	Ref<StandardMaterial3D> water = LookPreset3D::create_material(LookPreset3D::MATERIAL_WATER);
	CHECK(water->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA);
	CHECK(water->get_albedo().a < 1.0);
	CHECK(water->get_roughness() < 0.1);
	CHECK(water->get_albedo().b > water->get_albedo().r);

	// Reflections for water and glossy surfaces at high quality only.
	CHECK(LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH)->is_ssr_enabled());
	CHECK_FALSE(LookPreset3D::create_environment(LookPreset3D::QUALITY_STANDARD)->is_ssr_enabled());
}

TEST_CASE("[SceneTree][LookPreset3D] Night, three-point lighting and lamps") {
	// Night: a dark sky that the moon light does not turn into daytime, more exposure and glow.
	Ref<Environment> night = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_NIGHT);
	Ref<Environment> clear = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_CLEAR);
	Ref<ProceduralSkyMaterial> night_sky = night->get_sky()->get_material();
	REQUIRE(night_sky.is_valid());
	CHECK(night_sky->get_sky_top_color().get_v() < 0.1);
	CHECK(night->get_tonemap_exposure() > clear->get_tonemap_exposure());
	CHECK(night->get_glow_intensity() > clear->get_glow_intensity());
	CHECK(night->get_fog_light_color().get_v() < 0.2);

	DirectionalLight3D *moon = memnew(DirectionalLight3D);
	LookPreset3D::configure_sun(moon, LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_NIGHT);
	CHECK(moon->get_param(Light3D::PARAM_ENERGY) < 0.5);
	CHECK(moon->get_color().b > moon->get_color().r);
	memdelete(moon);

	// Fill light: from the side, in the color of the sky (blue shadows), no shadows, sky unaffected.
	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	DirectionalLight3D *fill = memnew(DirectionalLight3D);
	DirectionalLight3D *rim = memnew(DirectionalLight3D);
	LookPreset3D::configure_sun(sun, LookPreset3D::QUALITY_HIGH);
	LookPreset3D::configure_fill_light(fill);
	LookPreset3D::configure_rim_light(rim);
	CHECK_FALSE(fill->has_shadow());
	CHECK_FALSE(rim->has_shadow());
	CHECK(fill->get_sky_mode() == DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
	CHECK(rim->get_sky_mode() == DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
	CHECK(fill->get_color().b > fill->get_color().r);
	CHECK(fill->get_param(Light3D::PARAM_ENERGY) < sun->get_param(Light3D::PARAM_ENERGY));
	// Rim light comes from behind: opposite the key light.
	CHECK(rim->get_rotation_degrees().y == doctest::Approx(sun->get_rotation_degrees().y + 180.0));
	memdelete(sun);
	memdelete(fill);
	memdelete(rim);

	// Lamp: warm, light that falls off quickly, shadows except at low quality.
	OmniLight3D *lamp = memnew(OmniLight3D);
	LookPreset3D::configure_lamp(lamp, LookPreset3D::QUALITY_HIGH);
	CHECK(lamp->get_color().r > lamp->get_color().b);
	CHECK(lamp->get_param(Light3D::PARAM_ATTENUATION) > 1.0);
	CHECK(lamp->has_shadow());
	LookPreset3D::configure_lamp(lamp, LookPreset3D::QUALITY_LOW);
	CHECK_FALSE(lamp->has_shadow());
	memdelete(lamp);

	// The lamp itself is colored, not pure white.
	Ref<StandardMaterial3D> bulb = LookPreset3D::create_material(LookPreset3D::MATERIAL_LAMP);
	CHECK(bulb->get_feature(BaseMaterial3D::FEATURE_EMISSION));
	CHECK(bulb->get_emission().r > bulb->get_emission().b);
}

TEST_CASE("[SceneTree][LookPreset3D] Far distance blur at high quality only") {
	Ref<CameraAttributesPractical> high = LookPreset3D::create_camera_attributes(LookPreset3D::QUALITY_HIGH);
	REQUIRE(high.is_valid());
	CHECK(high->is_dof_blur_far_enabled());
	CHECK_FALSE(high->is_dof_blur_near_enabled());
	// The play area (well within 100 m) stays sharp.
	CHECK(high->get_dof_blur_far_distance() >= 100.0);
	CHECK(high->get_dof_blur_amount() < 0.1);
	CHECK_FALSE(high->is_auto_exposure_enabled());

	Ref<CameraAttributesPractical> standard = LookPreset3D::create_camera_attributes(LookPreset3D::QUALITY_STANDARD);
	CHECK_FALSE(standard->is_dof_blur_far_enabled());
	Ref<CameraAttributesPractical> low = LookPreset3D::create_camera_attributes(LookPreset3D::QUALITY_LOW);
	CHECK_FALSE(low->is_dof_blur_far_enabled());
}

TEST_CASE("[SceneTree][LookPreset3D] Camera lenses, eye levels and shift") {
	CHECK(LookPreset3D::get_lens_fov(LookPreset3D::LENS_WIDE) > LookPreset3D::get_lens_fov(LookPreset3D::LENS_STANDARD));
	CHECK(LookPreset3D::get_lens_fov(LookPreset3D::LENS_STANDARD) > LookPreset3D::get_lens_fov(LookPreset3D::LENS_TELEPHOTO));
	CHECK(LookPreset3D::get_eye_height(LookPreset3D::EYE_LEVEL_STANDING) == doctest::Approx(1.6));
	CHECK(LookPreset3D::get_eye_height(LookPreset3D::EYE_LEVEL_LOW) == doctest::Approx(0.8));

	Camera3D *camera = memnew(Camera3D);
	camera->set_near(0.1);
	camera->set_far(500.0);

	// Without a shift: an ordinary perspective camera.
	LookPreset3D::configure_camera(camera, LookPreset3D::LENS_TELEPHOTO);
	CHECK(camera->get_projection() == Camera3D::PROJECTION_PERSPECTIVE);
	CHECK(camera->get_fov() == doctest::Approx(25.0));
	CHECK(camera->get_near() == doctest::Approx(0.1));
	CHECK(camera->get_far() == doctest::Approx(500.0));

	// With a shift: same view size as the lens, framing moved up, camera not tilted.
	LookPreset3D::configure_camera(camera, LookPreset3D::LENS_STANDARD, 0.25);
	CHECK(camera->get_projection() == Camera3D::PROJECTION_FRUSTUM);
	const real_t expected_size = 2.0 * 0.1 * Math::tan(Math::deg_to_rad(45.0) * 0.5);
	CHECK(camera->get_size() == doctest::Approx(expected_size));
	CHECK(camera->get_frustum_offset().x == doctest::Approx(0.0));
	CHECK(camera->get_frustum_offset().y == doctest::Approx(0.25 * expected_size));
	CHECK(camera->get_rotation_degrees().is_zero_approx());

	memdelete(camera);
}

TEST_CASE("[SceneTree][LookPreset3D] Sky with clouds") {
	Ref<Environment> plain = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_CLEAR);
	CHECK(Object::cast_to<PhysicalSkyMaterial>(plain->get_sky()->get_material().ptr()) != nullptr);

	Ref<Environment> cloudy = LookPreset3D::create_environment(LookPreset3D::QUALITY_HIGH, LookPreset3D::ATMOSPHERE_CLEAR, true);
	Ref<ShaderMaterial> clear_sky = cloudy->get_sky()->get_material();
	REQUIRE(clear_sky.is_valid());
	REQUIRE(clear_sky->get_shader().is_valid());
	CHECK(clear_sky->get_shader()->get_mode() == Shader::MODE_SKY);
	// The rest of the look stays the same.
	CHECK(cloudy->get_tonemapper() == plain->get_tonemapper());
	CHECK(cloudy->get_fog_density() == doctest::Approx(plain->get_fog_density()));

	// Humid air has more cloud cover and a paler sky than clear air.
	Ref<ShaderMaterial> humid_sky = LookPreset3D::create_cloud_sky(LookPreset3D::ATMOSPHERE_HUMID);
	const float clear_coverage = clear_sky->get_shader_parameter("cloud_coverage");
	const float humid_coverage = humid_sky->get_shader_parameter("cloud_coverage");
	CHECK(humid_coverage > clear_coverage);
	const Color clear_zenith = clear_sky->get_shader_parameter("zenith_color");
	const Color humid_zenith = humid_sky->get_shader_parameter("zenith_color");
	CHECK(humid_zenith.get_s() < clear_zenith.get_s());
}

TEST_CASE("[SceneTree][LookPreset3D] Class defaults stay unchanged") {
	// Existing scenes do not store default values, so changing them would change how those scenes look.
	Ref<Environment> environment;
	environment.instantiate();
	CHECK(environment->get_tonemapper() == Environment::TONE_MAPPER_LINEAR);
	CHECK_FALSE(environment->is_glow_enabled());
	CHECK_FALSE(environment->is_fog_enabled());
	CHECK_FALSE(environment->is_ssao_enabled());
	CHECK_FALSE(environment->is_adjustment_enabled());
}

} // namespace TestLookPreset3D

#endif // _3D_DISABLED
