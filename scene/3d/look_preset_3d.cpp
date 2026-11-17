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

#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
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

namespace {

// Per-atmosphere values for the sky, the fog (aerial perspective) and the sun.
struct AtmosphereValues {
	// Physical sky. Small particles (Rayleigh) turn the distance blue and the
	// sky red near the horizon at a low sun; large particles (Mie: haze, water
	// vapor) turn it white.
	float rayleigh;
	float mie;
	float mie_eccentricity;
	float turbidity;
	// Fog. The aerial perspective blends distant objects toward the sky color
	// (blue in clear air); a light fog color with little aerial perspective
	// gives a white haze instead.
	Color fog_color;
	float fog_density;
	float fog_aerial_perspective;
	float fog_sky_affect;
	// Height fog: denser near the ground, like morning mist in humid air.
	float fog_height;
	float fog_height_density;
	float ambient_energy;
	// Sun (the moon at night).
	Vector3 sun_rotation_degrees;
	Color sun_color;
	float sun_energy;
	float exposure;
	float glow_intensity;
	// Night uses a dark procedural sky: the physical sky would turn the moon
	// light into a daytime sun.
	bool night;
};

const AtmosphereValues &get_atmosphere_values(Atmosphere p_atmosphere) {
	static const AtmosphereValues clear = {
		2.0, 0.003, 0.8, 8.0,
		Color(0.5, 0.62, 0.8), 0.0015, 0.85, 0.25,
		0.0, 0.0, 0.5,
		Vector3(-50.0, 150.0, 0.0), Color(1.0, 0.96, 0.9), 1.0,
		0.8, 0.6, false
	};
	static const AtmosphereValues humid = {
		1.2, 0.02, 0.75, 18.0,
		Color(0.82, 0.84, 0.86), 0.0022, 0.2, 0.5,
		5.0, 0.004, 0.5,
		Vector3(-40.0, 150.0, 0.0), Color(1.0, 0.97, 0.93), 1.0,
		0.8, 0.6, false
	};
	static const AtmosphereValues golden_hour = {
		2.0, 0.005, 0.8, 10.0,
		Color(0.92, 0.66, 0.46), 0.0018, 0.75, 0.3,
		0.0, 0.0, 0.8,
		Vector3(-12.0, 200.0, 0.0), Color(1.0, 0.72, 0.48), 1.6,
		0.8, 0.6, false
	};
	// Night: weak, cool moonlight and dark blue air. Light comes mostly from
	// lamps, so glow is stronger and only what the lamps reach is bright.
	static const AtmosphereValues night = {
		2.0, 0.003, 0.8, 8.0,
		Color(0.06, 0.08, 0.13), 0.004, 0.3, 0.6,
		0.0, 0.0, 0.25,
		Vector3(-40.0, 120.0, 0.0), Color(0.6, 0.7, 1.0), 0.2,
		1.3, 1.0, true
	};
	switch (p_atmosphere) {
		case ATMOSPHERE_HUMID:
			return humid;
		case ATMOSPHERE_GOLDEN_HOUR:
			return golden_hour;
		case ATMOSPHERE_NIGHT:
			return night;
		case ATMOSPHERE_CLEAR:
		default:
			return clear;
	}
}

} // namespace

Color grade_color(const Color &p_color) {
	// Display-referred (sRGB) input, as the tonemapper hands it to the LUT.
	const float luminance = p_color.r * 0.2126 + p_color.g * 0.7152 + p_color.b * 0.0722;
	const float shadow = 1.0 - Math::smoothstep(0.05f, 0.35f, luminance);
	const float highlight = Math::smoothstep(0.65f, 0.98f, luminance);

	// Keep saturation moderate in the mid-tones and lower it further in the
	// shadows and highlights, which look more natural closer to grey.
	const float saturation = 0.92 - 0.17 * shadow - 0.22 * highlight;
	Color graded = Color(luminance, luminance, luminance).lerp(p_color, saturation);

	// A gentle S-curve on the luminance for clearer light and shade.
	const float curved = luminance < 0.5 ? 0.5 * Math::pow(2.0f * luminance, 1.25f) : 1.0 - 0.5 * Math::pow(2.0f * (1.0f - luminance), 1.25f);
	const float target = Math::lerp(luminance, curved, 0.5f);
	const float gain = luminance > 0.0001 ? target / luminance : 1.0;
	graded.r *= gain;
	graded.g *= gain;
	graded.b *= gain;

	// Slightly cool shadows and slightly warm highlights.
	graded.r += -0.010 * shadow + 0.015 * highlight;
	graded.g += 0.005 * highlight;
	graded.b += 0.020 * shadow - 0.015 * highlight;

	graded.r = CLAMP(graded.r, 0.0f, 1.0f);
	graded.g = CLAMP(graded.g, 0.0f, 1.0f);
	graded.b = CLAMP(graded.b, 0.0f, 1.0f);
	graded.a = 1.0;
	return graded;
}

Ref<ImageTexture3D> create_color_grade() {
	// Small enough to keep scenes that embed it light (about 16 KB of texels).
	const int size = 16;
	Vector<Ref<Image>> slices;
	for (int b = 0; b < size; b++) {
		Ref<Image> slice = Image::create_empty(size, size, false, Image::FORMAT_RGBA8);
		for (int g = 0; g < size; g++) {
			for (int r = 0; r < size; r++) {
				// The renderer samples the LUT directly with the color, so texel i is
				// read at (i + 0.5) / size: store the graded color for that input.
				const Color input((r + 0.5) / size, (g + 0.5) / size, (b + 0.5) / size);
				slice->set_pixel(r, g, grade_color(input));
			}
		}
		slices.push_back(slice);
	}
	Ref<ImageTexture3D> lut;
	lut.instantiate();
	lut->create(Image::FORMAT_RGBA8, size, size, size, false, slices);
	return lut;
}

// Sky with clouds. The physical sky has no clouds, so this draws a sky gradient,
// the sun and a cloud layer: clouds are lit on the sun side and darker below,
// and crowd together toward the horizon because they lie on a plane above.
// Static on purpose: animating the sky would recompute its reflections every frame.
static const char *CLOUD_SKY_SHADER = R"(
shader_type sky;

uniform vec3 zenith_color : source_color = vec3(0.25, 0.45, 0.8);
uniform vec3 horizon_color : source_color = vec3(0.7, 0.8, 0.9);
uniform vec3 ground_color : source_color = vec3(0.3, 0.32, 0.25);
uniform float sky_energy = 1.0;
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.45;
uniform float cloud_scale = 0.6;
uniform vec3 cloud_light : source_color = vec3(1.0);
uniform vec3 cloud_shadow : source_color = vec3(0.55, 0.6, 0.7);

float hash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
	float v = 0.0;
	float a = 0.5;
	for (int k = 0; k < 5; k++) {
		v += a * noise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return v;
}

void sky() {
	vec3 d = EYEDIR;
	vec3 color = mix(horizon_color, zenith_color, sqrt(max(d.y, 0.0)));
	if (d.y < 0.0) {
		color = mix(horizon_color, ground_color, min(1.0, -d.y * 4.0));
	}
	vec3 sun_color = LIGHT0_ENABLED ? LIGHT0_COLOR * min(LIGHT0_ENERGY, 2.0) : vec3(0.0);
	if (LIGHT0_ENABLED) {
		float s = max(dot(d, LIGHT0_DIRECTION), 0.0);
		color += sun_color * (pow(s, 8.0) * 0.25 + pow(s, 64.0) * 0.5);
		color += sun_color * smoothstep(0.9995, 0.9998, s) * 8.0;
	}
	if (d.y > 0.0) {
		vec2 uv = d.xz / (d.y + 0.08) * cloud_scale;
		float n = fbm(uv * 3.0);
		float density = smoothstep(1.0 - cloud_coverage, 1.0 - cloud_coverage + 0.25, n);
		// Brighter where the cloud thins toward the sun (its lit side), darker inside and below.
		vec2 to_sun = LIGHT0_ENABLED ? LIGHT0_DIRECTION.xz : vec2(0.0);
		float lit = clamp(0.6 + (n - fbm((uv + to_sun * 0.04) * 3.0)) * 4.0, 0.0, 1.0);
		vec3 cloud = mix(cloud_shadow, cloud_light, lit) * (LIGHT0_ENABLED ? sun_color * 0.75 + vec3(0.15) : vec3(0.05));
		color = mix(color, cloud, density * smoothstep(0.0, 0.08, d.y));
	}
	COLOR = color * sky_energy;
}
)";

Ref<ShaderMaterial> create_cloud_sky(Atmosphere p_atmosphere) {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(CLOUD_SKY_SHADER);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	// Every parameter is set, so scenes keep the look even if the shader's defaults change.
	Color zenith(0.25, 0.45, 0.8);
	Color horizon(0.7, 0.8, 0.9);
	Color ground(0.3, 0.32, 0.25);
	Color cloud_shadow(0.55, 0.6, 0.7);
	float coverage = 0.45;
	switch (p_atmosphere) {
		case ATMOSPHERE_HUMID:
			zenith = Color(0.5, 0.58, 0.68);
			horizon = Color(0.82, 0.84, 0.86);
			cloud_shadow = Color(0.62, 0.65, 0.7);
			coverage = 0.6;
			break;
		case ATMOSPHERE_GOLDEN_HOUR:
			zenith = Color(0.3, 0.36, 0.55);
			horizon = Color(0.95, 0.62, 0.4);
			cloud_shadow = Color(0.45, 0.4, 0.5);
			break;
		case ATMOSPHERE_NIGHT:
			zenith = Color(0.01, 0.015, 0.04);
			horizon = Color(0.05, 0.07, 0.12);
			ground = Color(0.01, 0.01, 0.015);
			coverage = 0.35;
			break;
		case ATMOSPHERE_CLEAR:
		default:
			break;
	}
	material->set_shader_parameter("zenith_color", zenith);
	material->set_shader_parameter("horizon_color", horizon);
	material->set_shader_parameter("ground_color", ground);
	material->set_shader_parameter("sky_energy", 1.0);
	material->set_shader_parameter("cloud_coverage", coverage);
	material->set_shader_parameter("cloud_scale", 0.6);
	material->set_shader_parameter("cloud_light", Color(1.0, 1.0, 1.0));
	material->set_shader_parameter("cloud_shadow", cloud_shadow);
	return material;
}

Ref<Environment> create_environment(Quality p_quality, Atmosphere p_atmosphere, bool p_clouds) {
	const AtmosphereValues &values = get_atmosphere_values(p_atmosphere);

	Ref<Sky> sky;
	sky.instantiate();
	if (p_clouds) {
		sky->set_material(create_cloud_sky(p_atmosphere));
	} else if (values.night) {
		Ref<ProceduralSkyMaterial> sky_material;
		sky_material.instantiate();
		sky_material->set_sky_top_color(Color(0.01, 0.015, 0.04));
		sky_material->set_sky_horizon_color(Color(0.05, 0.07, 0.12));
		sky_material->set_ground_horizon_color(Color(0.05, 0.07, 0.12));
		sky_material->set_ground_bottom_color(Color(0.01, 0.01, 0.015));
		sky->set_material(sky_material);
	} else {
		Ref<PhysicalSkyMaterial> sky_material;
		sky_material.instantiate();
		sky_material->set_rayleigh_coefficient(values.rayleigh);
		sky_material->set_mie_coefficient(values.mie);
		sky_material->set_mie_eccentricity(values.mie_eccentricity);
		sky_material->set_turbidity(values.turbidity);
		sky_material->set_ground_color(Color(0.3, 0.32, 0.25));
		sky_material->set_energy_multiplier(2.5);
		sky->set_material(sky_material);
	}

	Ref<Environment> environment;
	environment.instantiate();
	environment->set_background(Environment::BG_SKY);
	environment->set_sky(sky);
	// Light the scene from the sky so shadowed areas are not flat grey.
	// Kept low so the sun still sets the contrast.
	environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	environment->set_ambient_light_energy(values.ambient_energy);
	environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);

	// Filmic tonemapping keeps bright highlights from clipping to white.
	// Slightly lower exposure gives a calmer, higher-contrast image.
	environment->set_tonemapper(Environment::TONE_MAPPER_ACES);
	environment->set_tonemap_exposure(values.exposure);

	// Restrained bloom on bright areas only.
	environment->set_glow_enabled(true);
	environment->set_glow_intensity(values.glow_intensity);
	environment->set_glow_bloom(0.05);
	environment->set_glow_hdr_bleed_threshold(1.0);

	// Color grade: saturation and light-and-shade as a 3D lookup table.
	environment->set_adjustment_enabled(true);
	environment->set_adjustment_color_correction(create_color_grade());

	// Distance fog for aerial perspective, so far objects separate from near ones.
	environment->set_fog_enabled(true);
	environment->set_fog_mode(Environment::FOG_MODE_EXPONENTIAL);
	environment->set_fog_light_color(values.fog_color);
	environment->set_fog_density(values.fog_density);
	environment->set_fog_aerial_perspective(values.fog_aerial_perspective);
	environment->set_fog_sky_affect(values.fog_sky_affect);
	environment->set_fog_height(values.fog_height);
	environment->set_fog_height_density(values.fog_height_density);

	// Contact shadows and bounce light. Not available in the Compatibility renderer.
	if (p_quality != QUALITY_LOW) {
		environment->set_ssao_enabled(true);
		environment->set_ssao_radius(1.1);
		environment->set_ssao_intensity(2.0);
	}
	if (p_quality == QUALITY_HIGH) {
		environment->set_ssil_enabled(true);
		environment->set_ssil_intensity(1.0);
		// Reflections of the scene on water and glossy floors.
		environment->set_ssr_enabled(true);
	}

	return environment;
}

void configure_sun(DirectionalLight3D *p_sun, Quality p_quality, Atmosphere p_atmosphere) {
	ERR_FAIL_NULL(p_sun);
	const AtmosphereValues &values = get_atmosphere_values(p_atmosphere);

	p_sun->set_rotation_degrees(values.sun_rotation_degrees);
	p_sun->set_color(values.sun_color);
	p_sun->set_param(Light3D::PARAM_ENERGY, values.sun_energy);

	p_sun->set_shadow(true);
	p_sun->set_param(Light3D::PARAM_SHADOW_BLUR, 1.0);
	switch (p_quality) {
		case QUALITY_HIGH:
			p_sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
			p_sun->set_blend_splits(true);
			p_sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 150.0);
			// Contact-hardening soft shadows: the farther a shadow falls from what
			// casts it, the softer it gets (sun-dappled light under high leaves).
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

Ref<StandardMaterial3D> create_material(MaterialKind p_kind) {
	Ref<StandardMaterial3D> material;
	material.instantiate();
	switch (p_kind) {
		case MATERIAL_SKIN:
			// Light scatters under the skin: the shadow edge turns warm and soft.
			material->set_albedo(Color(0.85, 0.64, 0.52));
			material->set_roughness(0.55);
			material->set_feature(BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING, true);
			material->set_subsurface_scattering_strength(0.6);
			material->set_flag(BaseMaterial3D::FLAG_SUBSURFACE_MODE_SKIN, true);
			break;
		case MATERIAL_FOLIAGE:
			// Thin leaves let light through: backlit leaves glow yellow-green.
			material->set_albedo(Color(0.32, 0.5, 0.18));
			material->set_roughness(0.6);
			material->set_feature(BaseMaterial3D::FEATURE_BACKLIGHT, true);
			material->set_backlight(Color(0.45, 0.6, 0.15));
			material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			break;
		case MATERIAL_GLASS:
			// Mostly transparent with crisp highlights and reflections.
			material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			material->set_albedo(Color(0.9, 0.95, 1.0, 0.15));
			material->set_roughness(0.03);
			material->set_specular(0.8);
			break;
		case MATERIAL_WATER:
			// Water color, what shows through from below, and reflection. The
			// shading model already reflects more at grazing angles, so far water
			// mirrors the sky while near water shows the bottom.
			material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			material->set_albedo(Color(0.1, 0.3, 0.33, 0.55));
			material->set_roughness(0.04);
			material->set_specular(0.6);
			break;
		case MATERIAL_LAMP:
			// The light source itself carries color too; pure white looks flat.
			material->set_albedo(Color(1.0, 0.85, 0.7));
			material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
			material->set_emission(Color(1.0, 0.7, 0.4));
			material->set_emission_energy_multiplier(3.0);
			break;
	}
	return material;
}

void configure_fill_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere) {
	ERR_FAIL_NULL(p_light);
	const AtmosphereValues &values = get_atmosphere_values(p_atmosphere);
	// From the side, low, in the color of the sky: it lifts the shadows a little
	// and makes them blue, like light from the sky. Kept weak: a directional
	// light reaches the whole scene, and a strong one flattens the contrast.
	p_light->set_rotation_degrees(Vector3(-25.0, values.sun_rotation_degrees.y + 90.0, 0.0));
	p_light->set_color(values.night ? Color(0.3, 0.4, 0.7) : Color(0.65, 0.75, 1.0));
	p_light->set_param(Light3D::PARAM_ENERGY, values.night ? 0.05 : 0.12);
	p_light->set_shadow(false);
	// Keep the sky driven by the key light (the sun) only.
	p_light->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
}

void configure_rim_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere) {
	ERR_FAIL_NULL(p_light);
	const AtmosphereValues &values = get_atmosphere_values(p_atmosphere);
	// From behind, opposite the key light, to outline silhouettes.
	p_light->set_rotation_degrees(Vector3(-35.0, values.sun_rotation_degrees.y + 180.0, 0.0));
	p_light->set_color(values.sun_color);
	p_light->set_param(Light3D::PARAM_ENERGY, values.night ? 0.1 : 0.3);
	p_light->set_shadow(false);
	p_light->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
}

void configure_lamp(OmniLight3D *p_lamp, Quality p_quality) {
	ERR_FAIL_NULL(p_lamp);
	// Warm, with a steep falloff so only what the light reaches is bright.
	p_lamp->set_color(Color(1.0, 0.72, 0.42));
	p_lamp->set_param(Light3D::PARAM_ENERGY, 1.5);
	p_lamp->set_param(Light3D::PARAM_RANGE, 8.0);
	p_lamp->set_param(Light3D::PARAM_ATTENUATION, 2.0);
	p_lamp->set_shadow(p_quality != QUALITY_LOW);
}

Ref<CameraAttributesPractical> create_camera_attributes(Quality p_quality) {
	Ref<CameraAttributesPractical> attributes;
	attributes.instantiate();
	if (p_quality == QUALITY_HIGH) {
		// Soften only the far distance, like a painter leaving distant scenery
		// less detailed; the play area stays sharp.
		attributes->set_dof_blur_far_enabled(true);
		attributes->set_dof_blur_far_distance(120.0);
		attributes->set_dof_blur_far_transition(200.0);
		attributes->set_dof_blur_amount(0.06);
	}
	return attributes;
}

float get_lens_fov(Lens p_lens) {
	switch (p_lens) {
		case LENS_WIDE:
			return 70.0;
		case LENS_TELEPHOTO:
			return 25.0;
		case LENS_STANDARD:
		default:
			return 45.0;
	}
}

float get_eye_height(EyeLevel p_eye_level) {
	return p_eye_level == EYE_LEVEL_LOW ? 0.8 : 1.6;
}

void configure_camera(Camera3D *p_camera, Lens p_lens, float p_shift) {
	ERR_FAIL_NULL(p_camera);
	const float fov = get_lens_fov(p_lens);
	const float z_near = p_camera->get_near();
	const float z_far = p_camera->get_far();
	if (Math::is_zero_approx(p_shift)) {
		p_camera->set_perspective(fov, z_near, z_far);
		return;
	}
	// Shift the view up or down instead of tilting the camera: vertical lines
	// stay parallel (a calm two-point look) while the framing looks up or down.
	// With the default keep-height aspect, the frustum size is the view height
	// at the near plane.
	const float size = 2.0 * z_near * Math::tan(Math::deg_to_rad(fov) * 0.5f);
	p_camera->set_frustum(size, Vector2(0.0, p_shift * size), z_near, z_far);
}

Vector<Vector2> get_composition_guide_lines(const Rect2 &p_frame) {
	Vector<Vector2> lines;
	const Vector2 p = p_frame.position;
	const Vector2 s = p_frame.size;
	for (int i = 1; i <= 2; i++) {
		const real_t x = p.x + s.x * i / 3.0;
		const real_t y = p.y + s.y * i / 3.0;
		lines.push_back(Vector2(x, p.y));
		lines.push_back(Vector2(x, p.y + s.y));
		lines.push_back(Vector2(p.x, y));
		lines.push_back(Vector2(p.x + s.x, y));
	}
	lines.push_back(p);
	lines.push_back(p + s);
	lines.push_back(Vector2(p.x + s.x, p.y));
	lines.push_back(Vector2(p.x, p.y + s.y));
	const Vector2 center = p + s * 0.5;
	const real_t arm = MIN(s.x, s.y) * 0.03;
	lines.push_back(center - Vector2(arm, 0));
	lines.push_back(center + Vector2(arm, 0));
	lines.push_back(center - Vector2(0, arm));
	lines.push_back(center + Vector2(0, arm));
	return lines;
}

} // namespace LookPreset3D
