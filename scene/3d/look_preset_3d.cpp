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

#include "core/config/project_settings.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/shader.h"
#include "scene/resources/sky.h"
#include "scene/resources/uzu_post_effect.h"
#include "servers/rendering/rendering_server.h"

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
			// 0.25 with the high filter (B-714): at 0.5 wide penumbras turned grainy
			// (grain 4.93 -> 1.37 measured on an RTX 4050, +0.74 ms).
			p_sun->set_param(Light3D::PARAM_SIZE, 0.25);
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

static const char *SHADOW_FILTER_SETTING = "rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality";

int get_shadow_filter_quality(Quality p_quality) {
	return p_quality == QUALITY_HIGH ? RSE::SHADOW_QUALITY_SOFT_HIGH : RSE::SHADOW_QUALITY_SOFT_LOW; // Soft Low is the engine default.
}

void apply_shadow_filter_quality(Quality p_quality) {
	const int filter = get_shadow_filter_quality(p_quality);
	ProjectSettings::get_singleton()->set_setting(SHADOW_FILTER_SETTING, filter);
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->directional_soft_shadow_filter_set_quality(RSE::ShadowQuality(filter));
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

static const char *WIND_FOLIAGE_SHADER = R"(
shader_type spatial;
render_mode cull_disabled;

// The look of the foliage material (create_material(MATERIAL_FOLIAGE)).
uniform vec4 albedo : source_color = vec4(0.32, 0.5, 0.18, 1.0);
uniform float roughness : hint_range(0.0, 1.0) = 0.6;
uniform vec4 backlight : source_color = vec4(0.45, 0.6, 0.15, 1.0);
// Wind.
uniform float wind_strength : hint_range(0.0, 1.0) = 0.12;
uniform float wind_speed : hint_range(0.0, 8.0) = 1.2;
uniform vec2 wind_direction = vec2(1.0, 0.3);
uniform float sway_height = 1.5; // Height above the base over which the sway reaches its full size.

void vertex() {
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float height = clamp((VERTEX.y + sway_height * 0.5) / sway_height, 0.0, 1.0);
	float phase = dot(world.xz, vec2(0.35, 0.21));
	float sway = sin(TIME * wind_speed + phase) * 0.7 + sin(TIME * wind_speed * 2.3 + phase * 1.7) * 0.3;
	VERTEX.xz += normalize(wind_direction) * sway * wind_strength * height * height;
}

void fragment() {
	ALBEDO = albedo.rgb;
	ROUGHNESS = roughness;
	BACKLIGHT = backlight.rgb;
}
)";

Ref<ShaderMaterial> create_wind_foliage_material(float p_strength, float p_speed) {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(WIND_FOLIAGE_SHADER);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	// The same look as the foliage preset.
	const Ref<StandardMaterial3D> foliage = create_material(MATERIAL_FOLIAGE);
	material->set_shader_parameter("albedo", foliage->get_albedo());
	material->set_shader_parameter("roughness", foliage->get_roughness());
	material->set_shader_parameter("backlight", foliage->get_backlight());
	material->set_shader_parameter("wind_strength", CLAMP(p_strength, 0.0f, 1.0f));
	material->set_shader_parameter("wind_speed", CLAMP(p_speed, 0.0f, 8.0f));
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

static const char *LIT_SPRITE_SHADER = R"(
shader_type spatial;
render_mode cull_disabled, depth_draw_opaque;

uniform sampler2D sprite_texture : source_color, filter_nearest, repeat_disable; // No wrap: the outline must not see the other edge.
uniform sampler2D normal_texture : hint_normal, filter_nearest, repeat_disable;
uniform bool use_normal_map = false;
uniform float alpha_scissor : hint_range(0.0, 1.0) = 0.5;
uniform float outline_width : hint_range(0.0, 4.0) = 0.0;
uniform vec4 outline_color : source_color = vec4(0.06, 0.05, 0.08, 1.0);
// A sprite is a flat card: light from behind passes through part of it, so a
// backlit sprite stays readable instead of turning into a silhouette.
uniform float backlight : hint_range(0.0, 1.0) = 0.5;

void vertex() {
	// Y-billboard that keeps the node's scale, facing the main camera also in shadow passes.
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
			vec4(normalize(cross(vec3(0.0, 1.0, 0.0), MAIN_CAM_INV_VIEW_MATRIX[2].xyz)), 0.0),
			vec4(0.0, 1.0, 0.0, 0.0),
			vec4(normalize(cross(MAIN_CAM_INV_VIEW_MATRIX[0].xyz, vec3(0.0, 1.0, 0.0))), 0.0),
			MODEL_MATRIX[3]);
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(
			vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0),
			vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0),
			vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0),
			vec4(0.0, 0.0, 0.0, 1.0));
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
}

void fragment() {
	vec4 color = texture(sprite_texture, UV) * COLOR;
	if (outline_width > 0.0 && color.a < alpha_scissor) {
		// A transparent texel next to an opaque one becomes the outline.
		vec2 step = outline_width / vec2(textureSize(sprite_texture, 0));
		float around = max(max(texture(sprite_texture, UV + vec2(step.x, 0.0)).a, texture(sprite_texture, UV - vec2(step.x, 0.0)).a),
				max(texture(sprite_texture, UV + vec2(0.0, step.y)).a, texture(sprite_texture, UV - vec2(0.0, step.y)).a));
		if (around >= alpha_scissor) {
			color = vec4(outline_color.rgb, 1.0);
		}
	}
	ALBEDO = color.rgb;
	ALPHA = color.a;
	ALPHA_SCISSOR_THRESHOLD = alpha_scissor;
	if (use_normal_map) {
		NORMAL_MAP = texture(normal_texture, UV).rgb;
	}
	ROUGHNESS = 1.0;
	SPECULAR = 0.2;
	BACKLIGHT = color.rgb * backlight;
}
)";

Ref<ShaderMaterial> create_lit_sprite_material(const Ref<Texture2D> &p_texture, const Ref<Texture2D> &p_normal_map, float p_outline_width, const Color &p_outline_color) {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(LIT_SPRITE_SHADER);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("sprite_texture", p_texture);
	material->set_shader_parameter("normal_texture", p_normal_map);
	material->set_shader_parameter("use_normal_map", p_normal_map.is_valid());
	material->set_shader_parameter("alpha_scissor", 0.5);
	material->set_shader_parameter("outline_width", CLAMP(p_outline_width, 0.0f, 4.0f));
	material->set_shader_parameter("outline_color", p_outline_color);
	material->set_shader_parameter("backlight", 0.5);
	return material;
}

void configure_lit_sprite(Sprite3D *p_sprite, const Ref<Texture2D> &p_normal_map, float p_outline_width) {
	ERR_FAIL_NULL(p_sprite);
	p_sprite->set_billboard_mode(StandardMaterial3D::BILLBOARD_DISABLED); // The material faces the camera.
	p_sprite->set_texture_filter(StandardMaterial3D::TEXTURE_FILTER_NEAREST);
	p_sprite->set_alpha_cut_mode(SpriteBase3D::ALPHA_CUT_DISCARD);
	p_sprite->set_draw_flag(SpriteBase3D::FLAG_SHADED, true);
	p_sprite->set_draw_flag(SpriteBase3D::FLAG_DOUBLE_SIDED, true);
	p_sprite->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
	// A decal projects if any layer overlaps. Use only the sprite layer.
	p_sprite->set_layer_mask(LIT_SPRITE_LAYER_MASK);
	p_sprite->set_material_override(create_lit_sprite_material(p_sprite->get_texture(), p_normal_map, p_outline_width));
}

Decal *create_blob_shadow(float p_radius) {
	const float radius = MAX(p_radius, 0.01f);
	Ref<Gradient> gradient;
	gradient.instantiate();
	gradient->set_color(0, Color(0.0, 0.0, 0.0, 0.55)); // Darkest under the feet...
	gradient->set_color(1, Color(0.0, 0.0, 0.0, 0.0)); // ...fading out at the rim.
	Ref<GradientTexture2D> texture;
	texture.instantiate();
	texture->set_gradient(gradient);
	texture->set_fill(GradientTexture2D::FILL_RADIAL);
	texture->set_fill_from(Vector2(0.5, 0.5));
	texture->set_fill_to(Vector2(0.5, 0.0));
	texture->set_width(64);
	texture->set_height(64);

	Decal *decal = memnew(Decal);
	decal->set_name("BlobShadow");
	decal->set_cull_mask(decal->get_cull_mask() & ~LIT_SPRITE_LAYER_MASK);
	decal->set_texture(Decal::TEXTURE_ALBEDO, texture);
	decal->set_size(Vector3(radius * 2.0, 1.0, radius * 2.0)); // Projects down onto the ground below.
	decal->set_lower_fade(0.5);
	decal->set_upper_fade(0.5);
	return decal;
}

struct ParticleValues {
	int amount;
	float lifetime;
	Vector3 gravity;
	Vector3 direction;
	float spread;
	float velocity_min;
	float velocity_max;
	float size;
	Color color; // Above 1 glows (the look's glow picks it up).
	bool additive;
	bool turbulence;
	bool blink;
};

static ParticleValues _particle_values(ParticleKind p_kind) {
	switch (p_kind) {
		case PARTICLES_FIREFLIES:
			return { 24, 6.0, Vector3(0, 0, 0), Vector3(0, 1, 0), 180.0, 0.05, 0.25, 0.06, Color(2.2, 2.6, 0.6), true, true, true };
		case PARTICLES_SNOW:
			return { 300, 8.0, Vector3(0, -0.6, 0), Vector3(0, -1, 0), 20.0, 0.1, 0.3, 0.05, Color(0.95, 0.97, 1.0), false, true, false };
		case PARTICLES_EMBERS:
			return { 60, 2.5, Vector3(0, 0.8, 0), Vector3(0, 1, 0), 25.0, 0.5, 1.2, 0.035, Color(3.0, 1.1, 0.25), true, true, true };
		case PARTICLES_DUST:
		default:
			return { 120, 10.0, Vector3(0, -0.01, 0), Vector3(0, 1, 0), 180.0, 0.01, 0.06, 0.025, Color(1.4, 1.3, 1.1), true, true, false };
	}
}

GPUParticles3D *create_particles(ParticleKind p_kind, const Vector3 &p_extents) {
	const ParticleValues values = _particle_values(p_kind);

	Ref<ParticleProcessMaterial> process;
	process.instantiate();
	process->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	process->set_emission_box_extents(p_extents);
	process->set_gravity(values.gravity);
	process->set_direction(values.direction);
	process->set_spread(values.spread);
	process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, values.velocity_min);
	process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, values.velocity_max);
	process->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.6);
	process->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0);
	process->set_color(values.color);
	process->set_turbulence_enabled(values.turbulence);
	process->set_turbulence_noise_strength(0.5);
	process->set_turbulence_noise_scale(4.0);
	// Fade in and out over the lifetime; blinking kinds pulse in between.
	Ref<Gradient> fade;
	fade.instantiate();
	fade->set_color(0, Color(1, 1, 1, 0));
	fade->set_color(1, Color(1, 1, 1, 0));
	if (values.blink) {
		fade->add_point(0.2, Color(1, 1, 1, 1));
		fade->add_point(0.35, Color(1, 1, 1, 0.15));
		fade->add_point(0.55, Color(1, 1, 1, 1));
		fade->add_point(0.8, Color(1, 1, 1, 0.3));
	} else {
		fade->add_point(0.15, Color(1, 1, 1, 1));
		fade->add_point(0.85, Color(1, 1, 1, 1));
	}
	Ref<GradientTexture1D> ramp;
	ramp.instantiate();
	ramp->set_gradient(fade);
	process->set_color_ramp(ramp);

	// A soft round dot facing the camera.
	Ref<Gradient> dot;
	dot.instantiate();
	dot->set_color(0, Color(1, 1, 1, 1));
	dot->set_color(1, Color(1, 1, 1, 0));
	Ref<GradientTexture2D> dot_texture;
	dot_texture.instantiate();
	dot_texture->set_gradient(dot);
	dot_texture->set_fill(GradientTexture2D::FILL_RADIAL);
	dot_texture->set_fill_from(Vector2(0.5, 0.5));
	dot_texture->set_fill_to(Vector2(0.5, 0.0));
	dot_texture->set_width(32);
	dot_texture->set_height(32);
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_blend_mode(values.additive ? BaseMaterial3D::BLEND_MODE_ADD : BaseMaterial3D::BLEND_MODE_MIX);
	material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, dot_texture);
	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(Vector2(values.size, values.size));
	quad->set_material(material);

	GPUParticles3D *particles = memnew(GPUParticles3D);
	particles->set_name("Particles");
	particles->set_amount(values.amount);
	particles->set_lifetime(values.lifetime);
	particles->set_pre_process_time(values.lifetime); // Already full when the scene starts.
	particles->set_process_material(process);
	particles->set_draw_pass_mesh(0, quad);
	particles->set_visibility_aabb(AABB(-p_extents - Vector3(2, 2, 2), (p_extents + Vector3(2, 2, 2)) * 2.0));
	particles->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	return particles;
}

static const char *GOD_RAYS_SHADER = R"(
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;

uniform vec4 ray_color : source_color = vec4(1.0, 0.86, 0.62, 1.0);
uniform float intensity : hint_range(0.0, 4.0) = 0.35;
uniform float streaks : hint_range(0.0, 1.0) = 0.45;

varying float side_view;

void vertex() {
	// Turn around the shaft's own axis (the node's Y) to face the camera.
	vec3 axis = MODEL_MATRIX[1].xyz;
	vec3 origin = MODEL_MATRIX[3].xyz;
	vec3 to_camera = CAMERA_POSITION_WORLD - origin;
	vec3 side = normalize(cross(normalize(axis), to_camera));
	vec3 normal = normalize(cross(side, normalize(axis)));
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(vec4(side * length(MODEL_MATRIX[0].xyz), 0.0), vec4(axis, 0.0), vec4(normal, 0.0), vec4(origin, 1.0));
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
	// A shaft seen from the side reads as a ray; one pointing at the camera would be
	// a flat smear, so it fades out.
	side_view = smoothstep(0.15, 0.6, 1.0 - abs(dot(normalize(axis), normalize(to_camera))));
}

void fragment() {
	// UV.y is 0 at the source (top) and 1 at the far end.
	float across = smoothstep(0.0, 0.35, 1.0 - abs(UV.x * 2.0 - 1.0));
	float along = smoothstep(0.0, 0.08, UV.y) * (1.0 - smoothstep(0.25, 1.0, UV.y));
	float streak = 1.0 - streaks + streaks * (0.5 + 0.5 * sin(UV.x * 37.0) * sin(UV.x * 13.0 + 1.7));
	ALBEDO = ray_color.rgb * intensity;
	ALPHA = across * along * streak * side_view;
}
)";

MeshInstance3D *create_god_rays(float p_width, float p_length, const Color &p_color) {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(GOD_RAYS_SHADER);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("ray_color", p_color);
	material->set_shader_parameter("intensity", 0.35);
	material->set_shader_parameter("streaks", 0.45);

	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(Vector2(MAX(p_width, 0.01f), MAX(p_length, 0.01f)));
	quad->set_center_offset(Vector3(0.0, -MAX(p_length, 0.01f) * 0.5f, 0.0)); // From the node downward along -Y.
	quad->set_material(material);

	MeshInstance3D *rays = memnew(MeshInstance3D);
	rays->set_name("GodRays");
	rays->set_mesh(quad);
	rays->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	rays->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	return rays;
}

static const char *WATER_SHADER = R"(
shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back, specular_schlick_ggx;

// The water's own color: shallow (thin water) and deep. How many meters of water
// the bottom stays visible through: clarity.
uniform vec4 shallow_color : source_color = vec4(0.2, 0.55, 0.52, 1.0);
uniform vec4 deep_color : source_color = vec4(0.015, 0.09, 0.13, 1.0);
uniform float clarity : hint_range(0.05, 50.0) = 2.0;
uniform float refraction : hint_range(0.0, 0.2) = 0.03;
// Waves: slope strength, size in meters, speed, and the swell of the surface itself.
uniform float wave_strength : hint_range(0.0, 3.0) = 0.6;
uniform float wave_scale : hint_range(0.05, 20.0) = 1.5;
uniform float wave_speed : hint_range(0.0, 4.0) = 1.0;
uniform float wave_height : hint_range(0.0, 1.0) = 0.04;
// Foam where the water is shallower than foam_width meters (shores, around rocks).
uniform float foam_width : hint_range(0.0, 3.0) = 0.3;
uniform vec4 foam_color : source_color = vec4(0.9, 0.95, 1.0, 1.0);
// Screen-space reflection of the scene (0 = the sky and probes only).
uniform float reflection : hint_range(0.0, 1.0) = 1.0;
uniform float surface_roughness : hint_range(0.0, 1.0) = 0.04;

uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap, repeat_disable;
uniform sampler2D depth_tex : hint_depth_texture, filter_nearest, repeat_disable;

varying vec3 world_pos;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Two wave trains crossing, three octaves each way.
float waves(vec2 p, float t) {
	float h = 0.0;
	float a = 0.5;
	for (int i = 0; i < 3; i++) {
		h += a * value_noise(p + vec2(t * 0.35, t * 0.2));
		h += a * value_noise(p * 1.3 - vec2(t * 0.25, -t * 0.3) + 17.0);
		p *= 2.1;
		a *= 0.45;
	}
	return h * 0.5;
}

// Distance from the camera (view-space depth) of what is in the depth buffer at uv.
float scene_depth(vec2 uv, mat4 inv_projection) {
	float d = textureLod(depth_tex, uv, 0.0).r;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
	vec3 ndc = vec3(uv, d) * 2.0 - 1.0;
#else
	vec3 ndc = vec3(uv * 2.0 - 1.0, d);
#endif
	vec4 v = inv_projection * vec4(ndc, 1.0);
	return -v.z / v.w;
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	VERTEX.y += (waves(world_pos.xz / (wave_scale * 4.0), TIME * wave_speed * 0.4) - 0.5) * 2.0 * wave_height;
}

void fragment() {
	float t = TIME * wave_speed;
	vec2 p = world_pos.xz / wave_scale;
	const float e = 0.04;
	float h0 = waves(p, t);
	vec2 slope = vec2(waves(p + vec2(e, 0.0), t) - h0, waves(p + vec2(0.0, e), t) - h0) / e;
	vec3 n_world = normalize(vec3(-slope.x * wave_strength * 0.12, 1.0, -slope.y * wave_strength * 0.12));
	NORMAL = normalize((VIEW_MATRIX * vec4(n_world, 0.0)).xyz);

	// How much water the eye looks through to reach the bottom (along the view ray).
	float surface = -VERTEX.z;
	float thick = max(scene_depth(SCREEN_UV, INV_PROJECTION_MATRIX) - surface, 0.0);
	// Refraction: bend the look-up by the waves, more where the water is deeper,
	// but never onto something in front of the water.
	vec3 flat_normal = normalize((VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
	vec2 ruv = SCREEN_UV + (NORMAL.xy - flat_normal.xy) * refraction * 3.0 * clamp(thick, 0.0, 1.0);
	float thick_r = scene_depth(ruv, INV_PROJECTION_MATRIX) - surface;
	if (thick_r < 0.0) {
		ruv = SCREEN_UV;
		thick_r = thick;
	}
	vec3 behind = textureLod(screen_tex, ruv, 0.0).rgb;
	float see = exp(-thick_r / clarity); // How much of the bottom shows.
	vec3 body = mix(deep_color.rgb, shallow_color.rgb, exp(-thick_r / (clarity * 2.5)));

	// Fresnel: water reflects little looking down and almost all at grazing angles.
	float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 5.0);

	// Screen-space reflection, marched in view space against the depth buffer.
	vec3 reflected = vec3(0.0);
	float hit = 0.0;
	if (reflection > 0.0) {
		vec3 dir = reflect(normalize(VERTEX), NORMAL);
		vec3 pos = VERTEX;
		float step_len = max(0.05, surface * 0.03);
		for (int i = 0; i < 28; i++) {
			pos += dir * step_len;
			step_len *= 1.12;
			vec4 clip = PROJECTION_MATRIX * vec4(pos, 1.0);
			if (clip.w <= 0.0) {
				break;
			}
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
				break;
			}
			float gap = -pos.z - scene_depth(uv, INV_PROJECTION_MATRIX);
			if (gap > 0.0 && gap < step_len * 2.0 + 0.1) {
				vec2 edge = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
				hit = edge.x * edge.y * reflection;
				reflected = textureLod(screen_tex, uv, 0.0).rgb;
				break;
			}
		}
	}

	// Foam: where the water is thin, broken up by noise and drifting.
	float edge = 1.0 - smoothstep(0.0, max(foam_width, 0.001), thick);
	float foam = smoothstep(0.5, 0.8, edge * (0.55 + 0.9 * value_noise(p * 7.0 + vec2(t * 0.3, -t * 0.2))));
	foam *= step(0.0001, foam_width);

	ALBEDO = mix(body * (1.0 - see), foam_color.rgb, foam);
	EMISSION = (behind * see * mix(vec3(1.0), shallow_color.rgb * 1.6, 0.35) * (1.0 - fresnel) + reflected * fresnel * hit) * (1.0 - foam);
	// Where the scene is reflected, the sky reflection from the specular term steps back.
	SPECULAR = 0.5 * (1.0 - hit * 0.9);
	ROUGHNESS = mix(surface_roughness, 0.7, foam);
	ALPHA = 1.0; // Drawn with the transparent objects, after the screen and depth are captured.
}
)";

Ref<ShaderMaterial> create_water_material() {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(WATER_SHADER);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	return material;
}

MeshInstance3D *create_water(const Vector2 &p_size) {
	const Vector2 size(MAX(p_size.x, 0.1f), MAX(p_size.y, 0.1f));
	Ref<PlaneMesh> plane;
	plane.instantiate();
	plane->set_size(size);
	// About two vertices per meter for the swell, within limits.
	plane->set_subdivide_width(CLAMP(int(size.x * 2.0f), 1, 200));
	plane->set_subdivide_depth(CLAMP(int(size.y * 2.0f), 1, 200));
	plane->set_material(create_water_material());

	MeshInstance3D *water = memnew(MeshInstance3D);
	water->set_name("Water");
	water->set_mesh(plane);
	water->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	water->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	return water;
}

Ref<Compositor> create_compositor(Quality p_quality) {
	Ref<Compositor> compositor;
	compositor.instantiate();
	if (p_quality != QUALITY_LOW) {
		Ref<UZUPostEffect> effect;
		effect.instantiate();
		TypedArray<CompositorEffect> effects;
		effects.push_back(effect);
		compositor->set_compositor_effects(effects);
	}
	return compositor;
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
