/**************************************************************************/
/*  look_preset_3d.cpp                                                    */
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

#include "look_preset_3d.h"

#include "scene/3d/light_3d.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"

namespace LookPreset3D {

Quality get_default_quality(const String &p_rendering_method) {
	if (p_rendering_method == "gl_compatibility" || p_rendering_method == "dummy") {
		return QUALITY_LOW;
	}
	if (p_rendering_method == "mobile") {
		return QUALITY_STANDARD;
	}
	return QUALITY_HIGH;
}

Ref<Environment> create_environment(Quality p_quality) {
	Ref<ProceduralSkyMaterial> sky_material;
	sky_material.instantiate();

	Ref<Sky> sky;
	sky.instantiate();
	sky->set_material(sky_material);

	Ref<Environment> environment;
	environment.instantiate();
	environment->set_background(Environment::BG_SKY);
	environment->set_sky(sky);
	// Light the scene from the sky so shadowed areas are not flat grey.
	// Kept at half strength so the sun still sets the contrast.
	environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	environment->set_ambient_light_energy(0.5);
	environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);

	// Filmic tonemapping keeps bright highlights from clipping to white.
	// Slightly lower exposure gives a calmer, higher-contrast image.
	environment->set_tonemapper(Environment::TONE_MAPPER_ACES);
	environment->set_tonemap_exposure(0.8);

	// Restrained bloom on bright areas only.
	environment->set_glow_enabled(true);
	environment->set_glow_intensity(0.6);
	environment->set_glow_bloom(0.05);
	environment->set_glow_hdr_bleed_threshold(1.0);

	// Distance fog for aerial perspective, so far objects separate from near ones.
	environment->set_fog_enabled(true);
	environment->set_fog_mode(Environment::FOG_MODE_EXPONENTIAL);
	environment->set_fog_light_color(Color(0.52, 0.6, 0.7));
	environment->set_fog_density(0.002);
	environment->set_fog_aerial_perspective(0.5);
	environment->set_fog_sky_affect(0.5);

	// Contact shadows and bounce light. Not available in the Compatibility renderer.
	if (p_quality != QUALITY_LOW) {
		environment->set_ssao_enabled(true);
		environment->set_ssao_radius(1.1);
		environment->set_ssao_intensity(2.0);
	}
	if (p_quality == QUALITY_HIGH) {
		environment->set_ssil_enabled(true);
		environment->set_ssil_intensity(1.0);
	}

	return environment;
}

void configure_sun(DirectionalLight3D *p_sun, Quality p_quality) {
	ERR_FAIL_NULL(p_sun);

	// Late-morning sun from the north-east, slightly warm.
	p_sun->set_rotation_degrees(Vector3(-50.0, 150.0, 0.0));
	p_sun->set_color(Color(1.0, 0.96, 0.9));
	p_sun->set_param(Light3D::PARAM_ENERGY, 1.0);

	p_sun->set_shadow(true);
	p_sun->set_param(Light3D::PARAM_SHADOW_BLUR, 1.0);
	switch (p_quality) {
		case QUALITY_HIGH:
			p_sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
			p_sun->set_blend_splits(true);
			p_sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 150.0);
			// Contact-hardening soft shadows.
			p_sun->set_param(Light3D::PARAM_SIZE, 0.5);
			break;
		case QUALITY_STANDARD:
			p_sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
			p_sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 100.0);
			break;
		case QUALITY_LOW:
			p_sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_2_SPLITS);
			p_sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 60.0);
			break;
	}
}

} // namespace LookPreset3D
