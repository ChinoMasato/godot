/**************************************************************************/
/*  uzu_picture.cpp                                                       */
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

#include "uzu_picture.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/math/random_number_generator.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/look_preset_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/uzu_post_effect.h"

// The two shaders of the prototype (ai_loop/prototypes/picture/), unchanged.
static const char *PICTURE_SHADER = R"UZU(// U.ZU Picture relief: the picture as a PBR surface.
// EMISSION keeps the picture's own (painted) light; ALBEDO / NORMAL / ROUGHNESS let real lights
// add highlights on top: lamps on wet stone, moon glints, a raking light over carvings.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D art : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap, repeat_disable;
uniform bool has_normal = false;
uniform float painted_light = 1.0;   // how much of the picture's own light is kept
uniform float relight = 0.55;        // how strongly real lights act on the picture
uniform bool backdrop = false;       // the layer behind torn edges: slightly darker and softer
uniform float flash = 0.0;           // lightning, 0..1+
uniform float cloud_amount = 0.0;    // extra drifting cloud layers over the sky region
uniform float cloud_speed = 1.0;
uniform vec2 cloud_dir = vec2(1.0, 0.0);

uniform sampler2D r_glow : filter_linear, repeat_disable;
uniform vec4 c_glow;
uniform sampler2D r_wet : filter_linear, repeat_disable;
uniform vec4 c_wet;
uniform sampler2D r_sky : filter_linear, repeat_disable;
uniform vec4 c_sky;
uniform sampler2D r_flicker : filter_linear, repeat_disable;
uniform vec4 c_flicker;
uniform sampler2D r_waterfall : filter_linear, repeat_disable;
uniform vec4 c_waterfall;
uniform sampler2D r_sway : filter_linear, repeat_disable;
uniform vec4 c_sway;
uniform sampler2D r_water : filter_linear, repeat_disable;
uniform vec4 c_water;

float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3. - 2. * f);
	return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y); }
float fbm(vec2 p) { float s = 0., a = .5; for (int i = 0; i < 4; i++) { s += a * vn(p); p = p * 2.03 + 17.1; a *= .5; } return s; }

float region(sampler2D t, vec4 c, vec2 uv) { return dot(texture(t, uv), c); }

void vertex() {
	// Banners and foliage sway; the amount grows with distance so it reads the same on screen.
	float b = region(r_sway, c_sway, UV);
	float z = -VERTEX.z;
	VERTEX.x += sin(TIME * 1.7 + UV.y * 28. + UV.x * 40.) * .0045 * z * b;
	VERTEX.z += cos(TIME * 1.3 + UV.y * 22.) * .01 * z * b;
}

void fragment() {
	vec2 uv = UV;
	float t = TIME;
	float sky = region(r_sky, c_sky, uv);
	float wet = region(r_wet, c_wet, uv);
	float water = region(r_water, c_water, uv);
	float fall = region(r_waterfall, c_waterfall, uv);
	// clouds churn, water ripples, waterfalls pull downward
	uv += (vec2(fbm(uv * vec2(3., 5.) + vec2(t * .015, 0.)), fbm(uv * vec2(4., 3.) - vec2(t * .01, t * .004))) - .5) * .014 * sky;
	uv.x += (vn(uv * vec2(60., 260.) + vec2(0., t * .9)) - .5) * .004 * (water + wet * .35);
	uv.y += (vn(uv * vec2(160., 20.) - vec2(0., t * 2.2)) - .5) * .004 * fall;
	vec3 alb = texture(art, uv).rgb;
	// Clouds: the painted clouds drift (two-phase flow, so they never stretch far), and two
	// procedural layers at different speeds roll over them; the far layer is slower.
	float cl = 0.0;
	// No branch here: texture reads inside a per-pixel branch break mip selection (blocky sky).
	{
		float sp = 0.006 * cloud_speed;
		float p0 = fract(t * 0.02), p1 = fract(t * 0.02 + 0.5);
		vec2 o = cloud_dir * 0.6;
		vec3 a0 = textureLod(art, uv - o * sp * 50.0 * p0, 0.0).rgb;
		vec3 a1 = textureLod(art, uv - o * sp * 50.0 * p1, 0.0).rgb;
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		vec3 drift = (a0 * w0 + a1 * (1.0 - w0));
		alb = mix(alb, drift, sky);
		float near_l = fbm(uv * vec2(3.0, 7.0) + cloud_dir * t * sp * 3.0);
		float far_l = fbm(uv * vec2(6.0, 12.0) + cloud_dir * t * sp * 1.2 + 9.0);
		cl = smoothstep(0.45, 0.9, near_l) * 0.65 + smoothstep(0.5, 0.95, far_l) * 0.35;
		// Dark rolling cloud bodies with a lit rim from the sky glow behind them.
		vec3 cloud_col = mix(vec3(0.03, 0.04, 0.06), alb * 1.25 + vec3(0.02, 0.025, 0.035), smoothstep(0.55, 0.85, near_l));
		alb = mix(alb, cloud_col, cl * cloud_amount * sky);
	}

	vec3 painted = alb * mix(1., .78 + .42 * fbm(uv * vec2(4., 7.) + vec2(t * .02, t * .004)), sky);
	// Lightning: clouds light up from inside, everything else catches a cold flash.
	painted += flash * (sky * vec3(0.5, 0.58, 0.78) * (0.6 + 1.6 * cl) + alb * 0.35);
	painted += vec3(.8, .86, .95) * smoothstep(.55, 1., vn(vec2(UV.x * 420., UV.y * 30. - t * 2.6))) * fall * .25;
	float fl = region(r_flicker, c_flicker, UV);
	painted *= 1. + fl * (vn(UV * vec2(110., 110.) + vec2(t * .6, t * .35)) - .5) * .55;
	float glow = region(r_glow, c_glow, UV);
	painted += alb * glow * (.6 * (vn(UV * 35. + vec2(t * 1.7, 0.)) - .5) + .5);
	float drop = step(.992, h21(floor(UV * vec2(900., 500.)) + floor(t * 9.)));
	painted += vec3(.75, .8, .9) * drop * wet * .4;

	if (backdrop) {
		painted *= 0.8;
	}
	EMISSION = painted * painted_light;
	ALBEDO = alb * relight * (1. - sky);
	if (has_normal) {
		vec3 n = texture(normal_tex, UV).rgb * 2. - 1.;
		vec3 nw = normalize(vec3(n.xy, n.z * 1.4));
		NORMAL = normalize((VIEW_MATRIX * vec4(nw, 0.)).xyz);
	}
	ROUGHNESS = mix(.92, .12, clamp(wet + water, 0., 1.));
	SPECULAR = mix(.2, .6, clamp(wet + water, 0., 1.));
	METALLIC = 0.;
}
)UZU";

static const char *FOG_SHEET_SHADER = R"UZU(// A sheet of drifting mist at one distance. Fades softly where it meets the picture (depth test
// against the scene), so it wraps around towers instead of cutting through them.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform float amount = 0.45;
uniform float speed = 0.01;
uniform float seed = 0.0;
uniform float top = 0.1;       // mist sits below this height (sheet uv, -1 bottom .. 1 top)
uniform vec4 color : source_color = vec4(0.13, 0.16, 0.19, 1.0);
uniform float flash = 0.0;

// Noise from a seamless texture (Godot FastNoiseLite), not a per-pixel hash.
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
float h21(vec2 p) { return texture(noise_tex, p * 0.37 + 0.5).g; }
float vn(vec2 p) { return texture(noise_tex, p / 9.0).r; }
float fbm(vec2 p) { float s = 0., a = .5; for (int i = 0; i < 5; i++) { s += a * vn(p); p = p * 2.03 + 17.1; a *= .5; } return s; }

void fragment() {
	vec2 q = UV * 2.0 - 1.0;
	q.y = -q.y;
	float n = fbm(vec2(q.x * 2.6 + TIME * speed * 10.0 + seed, q.y * 3.2 + seed * 0.3));
	n = smoothstep(0.35, 1.0, n + 0.1 * sin(TIME * 0.07 + seed));
	float band = smoothstep(top, top - 0.6, q.y) * smoothstep(-1.0, -0.6, q.y);
	// Soft contact with the picture behind or in front.
	float d = texture(depth_tex, SCREEN_UV).r;
	vec4 v = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	float scene_z = -v.z / v.w;
	float my_z = -VERTEX.z;
	float soft = clamp((scene_z - my_z) / (my_z * 0.3), 0.0, 1.0);
	ALBEDO = color.rgb + vec3(0.5, 0.58, 0.78) * flash * 0.6;
	ALPHA = n * band * amount * soft;
}
)UZU";

static const char *REGIONS[] = { "glow", "wet", "sky", "flicker", "waterfall", "sway", "water" };

namespace {

float num(const Dictionary &p_d, const String &p_key, float p_default) {
	const Variant v = p_d.get(p_key, p_default);
	return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) ? float(v) : p_default;
}

Dictionary dict(const Dictionary &p_d, const String &p_key) {
	const Variant v = p_d.get(p_key, Dictionary());
	return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}

Array arr(const Dictionary &p_d, const String &p_key, const Array &p_default = Array()) {
	const Variant v = p_d.get(p_key, p_default);
	return v.get_type() == Variant::ARRAY ? Array(v) : p_default;
}

Vector2 pixel(const Variant &p_at) {
	const Array a = p_at;
	return a.size() >= 2 ? Vector2(a[0], a[1]) : Vector2();
}

// Smooth noise that tiles seamlessly (cells wrap around), shared by the shaders for clouds,
// fog and ripples. In place of the prototype's Perlin noise (period about 50 px at 256 px),
// so the scene code does not depend on the noise module.
Ref<Image> seamless_noise(int p_size, int p_cells, uint64_t p_seed) {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->set_seed(p_seed);
	Vector<float> lattice;
	lattice.resize(p_cells * p_cells);
	for (int i = 0; i < lattice.size(); i++) {
		lattice.write[i] = rng->randf();
	}
	Vector<uint8_t> data;
	data.resize(p_size * p_size * 4);
	for (int y = 0; y < p_size; y++) {
		for (int x = 0; x < p_size; x++) {
			const float fx = float(x) / p_size * p_cells;
			const float fy = float(y) / p_size * p_cells;
			const int x0 = int(fx) % p_cells;
			const int y0 = int(fy) % p_cells;
			const int x1 = (x0 + 1) % p_cells;
			const int y1 = (y0 + 1) % p_cells;
			float tx = fx - Math::floor(fx);
			float ty = fy - Math::floor(fy);
			tx = tx * tx * (3.0f - 2.0f * tx);
			ty = ty * ty * (3.0f - 2.0f * ty);
			const float a = Math::lerp(lattice[y0 * p_cells + x0], lattice[y0 * p_cells + x1], tx);
			const float b = Math::lerp(lattice[y1 * p_cells + x0], lattice[y1 * p_cells + x1], tx);
			const uint8_t v = uint8_t(CLAMP(Math::lerp(a, b, ty), 0.0f, 1.0f) * 255.0f);
			const int k = (y * p_size + x) * 4;
			data.write[k] = v;
			data.write[k + 1] = v;
			data.write[k + 2] = v;
			data.write[k + 3] = 255;
		}
	}
	Ref<Image> image = Image::create_from_data(p_size, p_size, false, Image::FORMAT_RGBA8, data);
	image->generate_mipmaps();
	return image;
}

Ref<Shader> make_shader(const char *p_code) {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(p_code);
	return shader;
}

} // namespace

Ref<Image> UZUPicture::load_image(const String &p_path) {
	if (p_path.is_empty()) {
		return Ref<Image>();
	}
	// Prefer the resource pipeline: in an exported game the project's files
	// live inside the PCK, where only ResourceLoader can reach them — opening
	// the globalized path as a plain file misses them and the picture fails
	// to build (seen in the shipped sample: 'title/art.png' not found).
	// Imported images load as Texture2D; get_image() hands back the pixels.
	if (ResourceLoader::exists(p_path)) {
		Ref<Texture2D> texture = ResourceLoader::load(p_path);
		if (texture.is_valid()) {
			Ref<Image> image = texture->get_image();
			if (image.is_valid()) {
				if (image->is_compressed()) {
					image->decompress();
				}
				if (image->has_mipmaps()) {
					image->clear_mipmaps(); // Match the plain-file route below.
				}
				return image;
			}
		}
	}
	// Fall back to a plain file (user:// files, absolute paths, loose files
	// next to the project while editing).
	Ref<Image> image = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(p_path));
	if (image.is_valid() && image->is_compressed()) {
		image->decompress();
	}
	return image;
}

void UZUPicture::set_spec(const Dictionary &p_spec) {
	spec = p_spec;
}

Dictionary UZUPicture::get_spec() const {
	return spec;
}

void UZUPicture::set_spec_path(const String &p_path) {
	spec_path = p_path;
}

String UZUPicture::get_spec_path() const {
	return spec_path;
}

Dictionary UZUPicture::get_stats() const {
	return stats;
}

bool UZUPicture::is_built() const {
	return built;
}

Vector3 UZUPicture::place(const Vector2 &p_pixel, float p_depth) const {
	const float z = disparity ? 1.0f / (depth_a + depth_b * p_depth) : Math::lerp(near_z, far_z, p_depth);
	const float u = p_pixel.x / MAX(image_size.x, 1);
	const float v = p_pixel.y / MAX(image_size.y, 1);
	return Vector3((u * 2.0f - 1.0f) * tan_half * aspect * z, (1.0f - v * 2.0f) * tan_half * z, -z);
}

float UZUPicture::depth_at(const Vector2 &p_pixel) const {
	if (depth.is_null() || image_size.x <= 0 || image_size.y <= 0) {
		return 0.0;
	}
	const float x = CLAMP(p_pixel.x / image_size.x, 0.0f, 1.0f) * (depth->get_width() - 1);
	const float y = CLAMP(p_pixel.y / image_size.y, 0.0f, 1.0f) * (depth->get_height() - 1);
	const int x0 = int(x);
	const int y0 = int(y);
	const int x1 = MIN(x0 + 1, depth->get_width() - 1);
	const int y1 = MIN(y0 + 1, depth->get_height() - 1);
	const float fx = x - x0;
	const float fy = y - y0;
	const float a = Math::lerp(depth->get_pixel(x0, y0).r, depth->get_pixel(x1, y0).r, fx);
	const float b = Math::lerp(depth->get_pixel(x0, y1).r, depth->get_pixel(x1, y1).r, fx);
	return Math::lerp(a, b, fy);
}

Vector3 UZUPicture::point_at(const Vector2 &p_pixel) const {
	return place(p_pixel, depth_at(p_pixel));
}

void UZUPicture::_add_built(Node *p_node) {
	p_node->set_meta("_uzu_picture_built", true);
	add_child(p_node);
}

void UZUPicture::_clear() {
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *child = get_child(i);
		if (child->has_meta("_uzu_picture_built")) {
			remove_child(child);
			child->queue_free();
		}
	}
	relief_materials.clear();
	fog_materials.clear();
	lights.clear();
	camera = nullptr;
	bolt = nullptr;
	flash_at = -1.0;
	stats = Dictionary();
	built = false;
}

Dictionary UZUPicture::build() {
	_clear();
	if (spec.is_empty() && !spec_path.is_empty()) {
		const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(spec_path));
		if (parsed.get_type() == Variant::DICTIONARY) {
			spec = parsed;
		}
	}
	const Dictionary depth_spec = dict(spec, "depth");
	const Ref<Image> art = load_image(spec.get("image", ""));
	depth = load_image(depth_spec.get("path", ""));
	if (art.is_null() || depth.is_null()) {
		stats["ok"] = false;
		stats["error"] = "the spec needs \"image\" and \"depth\": {\"path\"} that can be read";
		return stats;
	}
	image_size = art->get_size();
	aspect = float(image_size.x) / image_size.y;
	const float fov = num(spec, "fov", 38.0);
	tan_half = Math::tan(Math::deg_to_rad(fov) * 0.5f);
	depth->convert(Image::FORMAT_RF);
	disparity = String(depth_spec.get("kind", "disparity")) == "disparity";
	near_z = num(depth_spec, "near", 4.5);
	far_z = num(depth_spec, "far", 160.0);
	depth_a = 1.0f / far_z;
	depth_b = 1.0f / near_z - 1.0f / far_z;

	const String quality = dict(spec, "look").get("quality", "high");
	noise = ImageTexture::create_from_image(seamless_noise(256, 6, 1));
	_build_relief(art, quality);
	_build_lights();
	_build_effects(quality);
	_build_camera(fov);
	_build_look();
	stats["ok"] = true;
	stats["fog_sheets"] = fog_materials.size();
	stats["lightning"] = bolt != nullptr;
	stats["lights"] = lights.size();
	Array size;
	size.push_back(image_size.x);
	size.push_back(image_size.y);
	stats["image_size"] = size;
	built = true;
	time = 0.0;
	set_process(true);
	return stats;
}

void UZUPicture::_set_regions(const Ref<ShaderMaterial> &p_material) {
	const Dictionary regions = dict(spec, "regions");
	HashMap<String, Ref<ImageTexture>> cache;
	for (const char *name : REGIONS) {
		Vector4 selector;
		Ref<ImageTexture> texture;
		if (regions.has(name)) {
			const Dictionary region = regions[name];
			const String path = region.get("path", "");
			if (!cache.has(path)) {
				Ref<Image> source = load_image(path);
				if (source.is_valid()) {
					source->convert(Image::FORMAT_RGBA8);
					cache[path] = ImageTexture::create_from_image(source);
				}
			}
			if (cache.has(path)) {
				texture = cache[path];
				const int channel = String("rgba").find(String(region.get("channel", "r")));
				if (channel >= 0) {
					selector[channel] = 1.0;
				}
			}
		}
		p_material->set_shader_parameter(String("r_") + name, texture);
		p_material->set_shader_parameter(String("c_") + name, selector);
	}
}

void UZUPicture::_set_clouds(const Ref<ShaderMaterial> &p_material) {
	const Dictionary clouds = dict(dict(spec, "effects"), "clouds");
	p_material->set_shader_parameter("cloud_amount", num(clouds, "amount", 0.0));
	p_material->set_shader_parameter("cloud_speed", num(clouds, "speed", 1.0));
	Array direction;
	direction.push_back(1);
	direction.push_back(0);
	direction = arr(clouds, "direction", direction);
	p_material->set_shader_parameter("cloud_dir", Vector2(direction[0], -float(direction[1])).normalized());
}

Vector<float> UZUPicture::min_filter(const Vector<float> &p_src, int p_w, int p_h, int p_r) {
	Vector<float> tmp;
	tmp.resize(p_src.size());
	for (int y = 0; y < p_h; y++) {
		for (int x = 0; x < p_w; x++) {
			float m = 1.0;
			for (int dx = -p_r; dx <= p_r; dx++) {
				m = MIN(m, p_src[y * p_w + CLAMP(x + dx, 0, p_w - 1)]);
			}
			tmp.write[y * p_w + x] = m;
		}
	}
	Vector<float> out;
	out.resize(p_src.size());
	for (int y = 0; y < p_h; y++) {
		for (int x = 0; x < p_w; x++) {
			float m = 1.0;
			for (int dy = -p_r; dy <= p_r; dy++) {
				m = MIN(m, tmp[CLAMP(y + dy, 0, p_h - 1) * p_w + x]);
			}
			out.write[y * p_w + x] = m;
		}
	}
	return out;
}

Ref<Image> UZUPicture::fill_behind(const Ref<Image> &p_art, const Vector<float> &p_d, const Vector<float> &p_back_d, int p_sx, int p_sy, float *r_filled_share) {
	const int w = MAX(1, p_art->get_width() / 2);
	const int h = MAX(1, p_art->get_height() / 2);
	Ref<Image> mask = Image::create_empty(p_sx + 1, p_sy + 1, false, Image::FORMAT_RF);
	for (int j = 0; j <= p_sy; j++) {
		for (int i = 0; i <= p_sx; i++) {
			const int k = j * (p_sx + 1) + i;
			mask->set_pixel(i, j, Color(p_d[k] - p_back_d[k] > 0.04f ? 1.0f : 0.0f, 0, 0));
		}
	}
	mask->resize(w, h, Image::INTERPOLATE_BILINEAR);
	Ref<Image> base = p_art->duplicate();
	base->resize(w, h, Image::INTERPOLATE_BILINEAR);
	base->convert(Image::FORMAT_RGBAF);
	int filled = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const float weight = mask->get_pixel(x, y).r < 0.1f ? 1.0f : 0.0f; // Grows the hole a little.
			const Color c = base->get_pixel(x, y);
			base->set_pixel(x, y, Color(c.r * weight, c.g * weight, c.b * weight, weight));
			filled += weight == 0.0f;
		}
	}
	if (r_filled_share) {
		*r_filled_share = float(filled) / (w * h);
	}
	// Pull: average down a pyramid (premultiplied, so holes do not darken it).
	Vector<Ref<Image>> levels;
	levels.push_back(base);
	while (levels[levels.size() - 1]->get_width() > 4 && levels[levels.size() - 1]->get_height() > 4) {
		Ref<Image> next = levels[levels.size() - 1]->duplicate();
		next->resize(MAX(1, next->get_width() / 2), MAX(1, next->get_height() / 2), Image::INTERPOLATE_BILINEAR);
		levels.push_back(next);
	}
	// Push: fill the holes on the way up from the normalized coarser level.
	Ref<Image> up = levels[levels.size() - 1];
	for (int li = levels.size() - 2; li >= 0; li--) {
		Ref<Image> level = levels[li];
		Ref<Image> coarse = up->duplicate();
		coarse->resize(level->get_width(), level->get_height(), Image::INTERPOLATE_BILINEAR);
		for (int y = 0; y < level->get_height(); y++) {
			for (int x = 0; x < level->get_width(); x++) {
				const Color f = level->get_pixel(x, y);
				const Color c = coarse->get_pixel(x, y);
				const float ca = MAX(c.a, 1e-4f);
				level->set_pixel(x, y, Color(f.r + (1.0f - f.a) * c.r / ca, f.g + (1.0f - f.a) * c.g / ca, f.b + (1.0f - f.a) * c.b / ca, 1.0f));
			}
		}
		up = level;
	}
	Ref<Image> out = levels[0];
	out->convert(Image::FORMAT_RGB8);
	return out;
}

Ref<ArrayMesh> UZUPicture::_grid_mesh(const Vector<float> &p_d, int p_sx, int p_sy, float p_tear, float p_push) {
	PackedVector3Array verts;
	PackedVector2Array uvs;
	verts.resize((p_sx + 1) * (p_sy + 1));
	uvs.resize(verts.size());
	for (int j = 0; j <= p_sy; j++) {
		for (int i = 0; i <= p_sx; i++) {
			const int k = j * (p_sx + 1) + i;
			const Vector2 uv(float(i) / p_sx, float(j) / p_sy);
			verts.write[k] = place(uv * Vector2(image_size), p_d[k]) * p_push;
			uvs.write[k] = uv;
		}
	}
	PackedInt32Array indices;
	int torn = 0;
	for (int j = 0; j < p_sy; j++) {
		for (int i = 0; i < p_sx; i++) {
			const int a = j * (p_sx + 1) + i;
			const int tris[2][3] = { { a, a + p_sx + 1, a + 1 }, { a + 1, a + p_sx + 1, a + p_sx + 2 } };
			for (const auto &tri : tris) {
				if (p_tear > 0.0f) {
					// Parallax on screen follows the disparity difference (not the distance ratio,
					// which is huge but harmless in the far sky), so cut where the disparity jumps.
					const float dmin = MIN(p_d[tri[0]], MIN(p_d[tri[1]], p_d[tri[2]]));
					const float dmax = MAX(p_d[tri[0]], MAX(p_d[tri[1]], p_d[tri[2]]));
					if (dmax - dmin > p_tear) {
						torn++;
						continue;
					}
				}
				indices.push_back(tri[0]);
				indices.push_back(tri[1]);
				indices.push_back(tri[2]);
			}
		}
	}
	PackedVector3Array normals;
	normals.resize(verts.size());
	normals.fill(Vector3(0, 0, 1));
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	if (p_tear > 0.0f) {
		stats["torn_triangles"] = torn;
	}
	return mesh;
}

void UZUPicture::_build_relief(const Ref<Image> &p_art, const String &p_quality) {
	const int sx = p_quality == "low" ? 220 : 360;
	const int sy = int(Math::round(sx / aspect));
	const Dictionary relief = dict(spec, "relief");
	Vector<float> d;
	d.resize((sx + 1) * (sy + 1));
	for (int j = 0; j <= sy; j++) {
		for (int i = 0; i <= sx; i++) {
			d.write[j * (sx + 1) + i] = depth_at(Vector2(float(i) / sx, float(j) / sy) * Vector2(image_size));
		}
	}
	stats["grid"] = Vector2i(sx, sy);
	// The front layer is torn where the depth jumps (the outlines of near things), so near
	// and far slide apart as separate layers instead of stretching like a rubber sheet.
	const float tear = num(relief, "tear", 0.05);
	Ref<ArrayMesh> front = _grid_mesh(d, sx, sy, tear, 1.0);
	// The backdrop: the same picture where near things are pushed back to the depth around
	// them (the smallest disparity nearby), so the gaps behind torn edges show background.
	const bool use_backdrop = relief.get("backdrop", true);
	Vector<float> back_d = use_backdrop ? min_filter(d, sx + 1, sy + 1, int(num(relief, "backdrop_radius", 6))) : d;

	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(make_shader(PICTURE_SHADER));
	material->set_shader_parameter("art", ImageTexture::create_from_image(p_art));
	_set_regions(material);
	material->set_shader_parameter("noise_tex", noise);
	if (spec.has("normal")) {
		const Ref<Image> normal = load_image(spec["normal"]);
		if (normal.is_valid()) {
			material->set_shader_parameter("normal_tex", ImageTexture::create_from_image(normal));
			material->set_shader_parameter("has_normal", true);
		}
	}
	_set_clouds(material);
	relief_materials.push_back(material);
	MeshInstance3D *front_instance = memnew(MeshInstance3D);
	front_instance->set_name("Relief");
	front_instance->set_mesh(front);
	front_instance->set_material_override(material);
	front_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	_add_built(front_instance);
	if (use_backdrop) {
		Ref<ShaderMaterial> back_material = material->duplicate();
		back_material->set_shader_parameter("backdrop", true);
		// The backdrop must not carry its own copy of the near things (it would show as a
		// ghost beside them when the camera moves): paint them out, fill from around.
		const uint64_t started = OS::get_singleton()->get_ticks_usec();
		float share = 0.0;
		back_material->set_shader_parameter("art", ImageTexture::create_from_image(fill_behind(p_art, d, back_d, sx, sy, &share)));
		stats["backdrop_fill_ms"] = (OS::get_singleton()->get_ticks_usec() - started) / 1000.0;
		stats["backdrop_filled_share"] = Math::snapped(share, 0.001f);
		relief_materials.push_back(back_material);
		MeshInstance3D *back_instance = memnew(MeshInstance3D);
		back_instance->set_name("Backdrop");
		back_instance->set_mesh(_grid_mesh(back_d, sx, sy, 0.0, 1.03));
		back_instance->set_material_override(back_material);
		back_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		_add_built(back_instance);
	}
}

void UZUPicture::_build_lights() {
	const Array spec_lights = arr(spec, "lights");
	for (int i = 0; i < spec_lights.size(); i++) {
		const Dictionary l = spec_lights[i];
		const Vector3 p = point_at(pixel(l.get("at", Array()))) * 0.93; // Just in front of the surface.
		OmniLight3D *light = memnew(OmniLight3D);
		light->set_name(vformat("Light%d", i));
		light->set_position(p);
		light->set_color(Color::html(l.get("color", "#ffb060")));
		const float energy = num(l, "energy", 1.0) * 1.5;
		light->set_param(Light3D::PARAM_ENERGY, energy);
		// The range grows with distance, so far lamps still light their surroundings.
		light->set_param(Light3D::PARAM_RANGE, MAX(1.5f, -p.z * 0.35f) * num(l, "reach", 1.0));
		light->set_param(Light3D::PARAM_ATTENUATION, 1.6);
		light->set_shadow(false);
		_add_built(light);
		lights.push_back({ light, energy, num(l, "flicker", 0.0), i * 13.0f });
	}
	if (spec.has("moon")) {
		const Dictionary m = spec["moon"];
		const Vector3 direction = place(pixel(m.get("at", Array())), 0.0).normalized();
		DirectionalLight3D *moon = memnew(DirectionalLight3D);
		moon->set_name("Moon");
		moon->set_color(Color::html(m.get("color", "#a8bce0")));
		moon->set_param(Light3D::PARAM_ENERGY, num(m, "energy", 0.6));
		moon->set_shadow(false);
		_add_built(moon);
		moon->set_transform(Transform3D().looking_at(-direction, Vector3(0, 1, 0)));
	}
}

static Ref<StandardMaterial3D> ember_material() {
	Ref<StandardMaterial3D> m;
	m.instantiate();
	m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	m->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
	m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
	m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	m->set_albedo(Color(6.0, 2.2, 0.5));
	Ref<Gradient> g;
	g.instantiate();
	g->set_color(0, Color(1, 1, 1, 1));
	g->set_color(1, Color(1, 1, 1, 0));
	Ref<GradientTexture2D> texture;
	texture.instantiate();
	texture->set_gradient(g);
	texture->set_fill(GradientTexture2D::FILL_RADIAL);
	texture->set_fill_from(Vector2(0.5, 0.5));
	texture->set_fill_to(Vector2(1.0, 0.5));
	m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
	return m;
}

void UZUPicture::_build_effects(const String &p_quality) {
	const Dictionary fx = dict(spec, "effects");
	const bool low = p_quality == "low";
	if (fx.has("rain")) {
		const Dictionary rain_spec = fx["rain"];
		GPUParticles3D *rain = memnew(GPUParticles3D);
		rain->set_name("Rain");
		rain->set_amount(int(num(rain_spec, "amount", 1600) * (low ? 0.45f : 1.0f)));
		rain->set_lifetime(1.6);
		rain->set_pre_process_time(2.0);
		rain->set_visibility_aabb(AABB(Vector3(-20, -12, -40), Vector3(40, 30, 42)));
		Ref<ParticleProcessMaterial> pm;
		pm.instantiate();
		pm->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
		pm->set_emission_box_extents(Vector3(13, 1, 15));
		pm->set_direction(Vector3(0.12, -1, 0));
		pm->set_spread(3.0);
		pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 9.0);
		pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 14.0);
		pm->set_gravity(Vector3());
		pm->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
		rain->set_process_material(pm);
		rain->set_position(Vector3(0, 9, -18));
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(0.012, 0.45));
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
		m->set_albedo(Color(0.72, 0.8, 0.88, num(rain_spec, "opacity", 0.14)));
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		quad->set_material(m);
		rain->set_draw_pass_mesh(0, quad);
		_add_built(rain);
		stats["rain"] = rain->get_amount();
	}
	const Dictionary fog = dict(fx, "fog");
	if (fog.has("layers")) {
		// Sheets of mist at several distances, each drifting at its own speed: as the camera
		// moves, they slide past each other and past the picture, which reads as deep space.
		const Array layers = fog["layers"];
		for (int k = 0; k < layers.size(); k++) {
			const float zf = layers[k];
			Ref<QuadMesh> quad;
			quad.instantiate();
			quad->set_size(Vector2(2.0f * tan_half * aspect * zf * 1.35f, 2.0f * tan_half * zf * 1.35f));
			Ref<ShaderMaterial> m;
			m.instantiate();
			m->set_shader(make_shader(FOG_SHEET_SHADER));
			m->set_shader_parameter("amount", num(fog, "amount", 0.45));
			m->set_shader_parameter("speed", 0.05f / Math::sqrt(zf) * num(fog, "speed", 1.0));
			m->set_shader_parameter("seed", k * 7.3f);
			m->set_shader_parameter("top", num(fog, "top", 0.1));
			m->set_shader_parameter("color", Color::html(fog.get("color", "#222a33")));
			m->set_shader_parameter("noise_tex", noise);
			fog_materials.push_back(m);
			MeshInstance3D *sheet = memnew(MeshInstance3D);
			sheet->set_name(vformat("Fog%d", k));
			sheet->set_mesh(quad);
			sheet->set_material_override(m);
			sheet->set_position(Vector3(0, 0, -zf));
			sheet->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			_add_built(sheet);
		}
	}
	if (fx.has("lightning")) {
		const Dictionary lt = fx["lightning"];
		flash_seed = uint64_t(num(lt, "seed", 1));
		flash_at = num(lt, "first", 2.0);
		bolt = memnew(DirectionalLight3D);
		bolt->set_name("Lightning");
		bolt->set_color(Color(0.75, 0.82, 1.0));
		bolt->set_param(Light3D::PARAM_ENERGY, 0.0);
		bolt->set_shadow(false);
		_add_built(bolt);
		Array fallback;
		fallback.push_back(image_size.x * 0.5f);
		fallback.push_back(0);
		const Array at = lt.get("at", dict(spec, "moon").get("at", fallback));
		const Vector3 direction = place(pixel(at), 0.0).normalized();
		bolt->set_transform(Transform3D().looking_at(-direction + Vector3(0, -0.3, 0), Vector3(0, 1, 0)));
	}
	const Dictionary embers = dict(fx, "embers");
	if (embers.has("from")) {
		const Array from = embers["from"];
		const int per = MAX(4, int(num(embers, "amount", 48) * (low ? 0.4f : 1.0f)) / MAX(1, from.size()));
		for (int k = 0; k < from.size(); k++) {
			const Vector3 p = point_at(pixel(from[k])) * 0.95;
			GPUParticles3D *e = memnew(GPUParticles3D);
			e->set_name(vformat("Embers%d", k));
			e->set_amount(per);
			e->set_lifetime(3.0);
			e->set_pre_process_time(3.0);
			e->set_position(p);
			Ref<ParticleProcessMaterial> pm;
			pm.instantiate();
			pm->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE);
			pm->set_emission_sphere_radius(0.02f * -p.z);
			pm->set_direction(Vector3(0, 1, 0));
			pm->set_spread(25.0);
			pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.02f * -p.z);
			pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.06f * -p.z);
			pm->set_gravity(Vector3(0.01, 0.01, 0) * -p.z);
			pm->set_turbulence_enabled(true);
			pm->set_turbulence_noise_strength(0.6);
			pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.6);
			pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.2);
			Ref<Gradient> fade;
			fade.instantiate();
			fade->set_color(0, Color(1, 1, 1, 1));
			fade->set_color(1, Color(1, 0.4, 0.1, 0));
			Ref<GradientTexture1D> ramp;
			ramp.instantiate();
			ramp->set_gradient(fade);
			pm->set_color_ramp(ramp);
			e->set_process_material(pm);
			Ref<QuadMesh> quad;
			quad.instantiate();
			quad->set_size(Vector2(1, 1) * 0.004f * -p.z);
			quad->set_material(ember_material());
			e->set_draw_pass_mesh(0, quad);
			e->set_visibility_aabb(AABB(Vector3(-5, -2, -5), Vector3(10, 10, 10)));
			_add_built(e);
		}
		stats["ember_sources"] = from.size();
	}
}

void UZUPicture::_build_camera(float p_fov) {
	camera = memnew(Camera3D);
	camera->set_name("Camera");
	// Show a little less than the picture, so its edges stay out of view while the camera drifts.
	const float overscan = num(dict(spec, "camera"), "overscan", 0.07);
	camera->set_fov(Math::rad_to_deg(2.0f * Math::atan(tan_half * (1.0f - overscan))));
	camera->set_near(0.05);
	camera->set_far(400.0);
	_add_built(camera);
	if (is_inside_tree()) {
		camera->make_current();
	} else {
		camera->set_current(true);
	}
}

void UZUPicture::_build_look() {
	const Dictionary look = dict(spec, "look");
	const String quality_name = look.get("quality", "high");
	const String atmosphere_name = look.get("atmosphere", "night");
	const LookPreset3D::Quality quality = quality_name == "low" ? LookPreset3D::QUALITY_LOW : (quality_name == "standard" ? LookPreset3D::QUALITY_STANDARD : LookPreset3D::QUALITY_HIGH);
	LookPreset3D::Atmosphere atmosphere = LookPreset3D::ATMOSPHERE_NIGHT;
	if (atmosphere_name == "clear") {
		atmosphere = LookPreset3D::ATMOSPHERE_CLEAR;
	} else if (atmosphere_name == "humid") {
		atmosphere = LookPreset3D::ATMOSPHERE_HUMID;
	} else if (atmosphere_name == "golden_hour") {
		atmosphere = LookPreset3D::ATMOSPHERE_GOLDEN_HOUR;
	}
	Ref<Environment> env = LookPreset3D::create_environment(quality, atmosphere, false);
	// The picture already carries its own light and air: keep the engine's additions gentle.
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(Color(0.02, 0.025, 0.04));
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(0, 0, 0));
	env->set_ambient_light_energy(0.0);
	env->set_ssao_enabled(false);
	env->set_ssil_enabled(false);
	env->set_ssr_enabled(false);
	env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	env->set_tonemap_exposure(1.0);
	env->set_adjustment_enabled(false);
	env->set_fog_density(env->get_fog_density() * num(dict(dict(spec, "effects"), "fog"), "density", 0.0));
	env->set_glow_enabled(true);
	env->set_glow_hdr_bleed_threshold(1.0);
	env->set_glow_intensity(num(look, "bloom", 0.8));
	WorldEnvironment *world_environment = memnew(WorldEnvironment);
	world_environment->set_name("Look");
	world_environment->set_environment(env);
	if (bool(look.get("post", true))) {
		Ref<Compositor> compositor;
		compositor.instantiate();
		Ref<UZUPostEffect> post;
		post.instantiate();
		post->set_edge_strength(num(look, "edge", 0.15));
		post->set_vignette_strength(num(look, "vignette", 0.4));
		TypedArray<CompositorEffect> effects;
		effects.push_back(post);
		compositor->set_compositor_effects(effects);
		world_environment->set_compositor(compositor);
	}
	_add_built(world_environment);
}

// Lightning: a strike is two quick pulses and a fading glow. Strikes come every "every"
// seconds (random within the range, fixed by "seed"), the first at "first", so a shot can
// catch one.
void UZUPicture::_update_lightning() {
	if (flash_at < 0.0f) {
		return;
	}
	const Dictionary lt = dict(dict(spec, "effects"), "lightning");
	const float s = time - flash_at;
	float f = 0.0;
	if (s >= 0.0f) {
		f = MAX(Math::exp(-s * 30.0f), 0.75f * Math::exp(-MAX(s - 0.16f, 0.0f) * 9.0f) * float(s > 0.16f)) + 0.25f * Math::exp(-s * 2.5f);
		if (s > 2.0f) {
			Array range;
			range.push_back(6);
			range.push_back(14);
			const Array every = lt.get("every", range);
			Ref<RandomNumberGenerator> rng;
			rng.instantiate();
			rng->set_seed(flash_seed++);
			flash_at = time + rng->randf_range(every[0], every[1]);
		}
	}
	f *= num(lt, "strength", 1.0);
	for (const Ref<ShaderMaterial> &m : relief_materials) {
		m->set_shader_parameter("flash", f);
	}
	for (const Ref<ShaderMaterial> &m : fog_materials) {
		m->set_shader_parameter("flash", f);
	}
	if (bolt) {
		bolt->set_param(Light3D::PARAM_ENERGY, f * 3.0f);
	}
}

void UZUPicture::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!built && (!spec.is_empty() || !spec_path.is_empty())) {
				const Dictionary result = build();
				if (!bool(result.get("ok", false))) {
					ERR_PRINT(vformat("UZUPicture: %s", String(result.get("error", "could not build"))));
				}
			}
		} break;
		case NOTIFICATION_PROCESS: {
			if (!built) {
				return;
			}
			time += get_process_delta_time();
			const Dictionary cam = dict(spec, "camera");
			const float drift = num(cam, "drift", 1.0);
			const float push_k = num(cam, "push", 1.0);
			const float sw = Math::sin(time * Math::TAU / 23.0f);
			const float push = 0.5f - 0.5f * Math::cos(time * Math::TAU / 31.0f);
			if (camera) {
				const Vector3 position(sw * 0.2f * drift, Math::sin(time * Math::TAU / 19.0f) * 0.05f * drift, -0.05f - push * 0.32f * push_k);
				camera->set_transform(Transform3D(Basis(), position).looking_at(Vector3(position.x * 0.25f, position.y * 0.25f, -26.0f), Vector3(0, 1, 0)));
			}
			_update_lightning();
			for (const LightState &l : lights) {
				if (l.flicker > 0.0f) {
					const float n = Math::sin(time * 7.1f + l.seed) * 0.5f + Math::sin(time * 19.3f + l.seed * 2.1f) * 0.25f + Math::sin(time * 31.7f + l.seed * 0.7f) * 0.15f;
					l.node->set_param(Light3D::PARAM_ENERGY, l.energy * (1.0f + l.flicker * 0.35f * n));
				}
			}
		} break;
	}
}

void UZUPicture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_spec", "spec"), &UZUPicture::set_spec);
	ClassDB::bind_method(D_METHOD("get_spec"), &UZUPicture::get_spec);
	ClassDB::bind_method(D_METHOD("set_spec_path", "path"), &UZUPicture::set_spec_path);
	ClassDB::bind_method(D_METHOD("get_spec_path"), &UZUPicture::get_spec_path);
	ClassDB::bind_method(D_METHOD("build"), &UZUPicture::build);
	ClassDB::bind_method(D_METHOD("get_stats"), &UZUPicture::get_stats);
	ClassDB::bind_method(D_METHOD("is_built"), &UZUPicture::is_built);
	ClassDB::bind_method(D_METHOD("place", "pixel", "depth"), &UZUPicture::place);
	ClassDB::bind_method(D_METHOD("depth_at", "pixel"), &UZUPicture::depth_at);
	ClassDB::bind_method(D_METHOD("point_at", "pixel"), &UZUPicture::point_at);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "spec"), "set_spec", "get_spec");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "spec_path", PROPERTY_HINT_FILE, "*.json"), "set_spec_path", "get_spec_path");
}

UZUPicture::UZUPicture() {
	set_process(false);
}
