/**************************************************************************/
/*  uzu_pbr_from_image.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* the "Software" to deal in the Software without restriction, including  */
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
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "uzu_pbr_from_image.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/io/resource_saver.h"
#include "core/math/math_funcs.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"

namespace {

struct KindDef {
	float strength; // Normal strength fitted against scans.
	float rough; // Roughness base.
	float spread; // How much the cavities and height move the roughness.
	float metal; // Metallic (0 or 1 in the fitted table).
	float detail; // Fine grain share of the height (vs the broad shapes).
};

struct KindEntry {
	const char *name;
	KindDef def;
};

// Fitted 2026-11-18 against Poly Haven scans (brick, cobblestone, rock) in the
// prototype; keep in sync with ai_loop/prototypes/picture/pbr_tool.gd.
const KindEntry KIND_TABLE[] = {
	{ "stone", { 0.55f, 0.82f, 0.18f, 0.0f, 0.5f } },
	{ "brick", { 0.65f, 0.86f, 0.14f, 0.0f, 0.4f } },
	{ "cobble", { 0.8f, 0.62f, 0.22f, 0.0f, 0.4f } },
	{ "ground", { 0.45f, 0.9f, 0.1f, 0.0f, 0.6f } },
	{ "wood", { 0.3f, 0.7f, 0.2f, 0.0f, 0.7f } },
	{ "plaster", { 0.25f, 0.88f, 0.08f, 0.0f, 0.6f } },
	{ "fabric", { 0.2f, 0.95f, 0.05f, 0.0f, 0.9f } },
	{ "leather", { 0.25f, 0.6f, 0.2f, 0.0f, 0.8f } },
	{ "metal", { 0.2f, 0.4f, 0.35f, 1.0f, 0.7f } },
	{ "painted_metal", { 0.2f, 0.55f, 0.3f, 0.0f, 0.7f } },
};

const KindDef *find_kind(const String &p_kind) {
	for (const KindEntry &entry : KIND_TABLE) {
		if (p_kind == entry.name) {
			return &entry.def;
		}
	}
	return nullptr;
}

Dictionary error_result(const String &p_error) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	return result;
}

Ref<Image> buf_to_image(const Vector<float> &p_buf, int p_w, int p_h) {
	PackedByteArray bytes;
	bytes.resize(p_buf.size() * (int)sizeof(float));
	memcpy(bytes.ptrw(), p_buf.ptr(), p_buf.size() * sizeof(float));
	return Image::create_from_data(p_w, p_h, false, Image::FORMAT_RF, bytes);
}

Vector<float> image_to_buf(const Ref<Image> &p_img) {
	const PackedByteArray data = p_img->get_data();
	Vector<float> out;
	out.resize((int)(data.size() / (int)sizeof(float)));
	memcpy(out.ptrw(), data.ptr(), data.size());
	return out;
}

// Blur by shrinking and growing again (native and fast), like the prototype.
Vector<float> blur_buf(const Vector<float> &p_buf, int p_w, int p_h, int p_factor, bool p_tile) {
	Ref<Image> img = buf_to_image(p_buf, p_w, p_h);
	if (p_tile) {
		// Pad with wrapped copies so the blur also wraps around the edges.
		Ref<Image> big = Image::create_empty(p_w * 3, p_h * 3, false, Image::FORMAT_RF);
		for (int j = 0; j < 3; j++) {
			for (int i = 0; i < 3; i++) {
				big->blit_rect(img, Rect2i(0, 0, p_w, p_h), Vector2i(i * p_w, j * p_h));
			}
		}
		big->resize(MAX(3, p_w * 3 / p_factor), MAX(3, p_h * 3 / p_factor), Image::INTERPOLATE_BILINEAR);
		big->resize(p_w * 3, p_h * 3, Image::INTERPOLATE_CUBIC);
		img = big->get_region(Rect2i(p_w, p_h, p_w, p_h));
	} else {
		img->resize(MAX(2, p_w / p_factor), MAX(2, p_h / p_factor), Image::INTERPOLATE_BILINEAR);
		img->resize(p_w, p_h, Image::INTERPOLATE_CUBIC);
	}
	return image_to_buf(img);
}

float percentile_buf(const Vector<float> &p_buf, float p_p) {
	Vector<float> sorted = p_buf;
	sorted.sort();
	const int idx = CLAMP(int(sorted.size() * p_p), 0, sorted.size() - 1);
	return sorted[idx];
}

uint8_t to_byte(float p_v) {
	return (uint8_t)CLAMP(p_v * 255.0f, 0.0f, 255.0f);
}

} // namespace

Dictionary UZUPBRFromImage::kinds() {
	Dictionary result;
	for (const KindEntry &entry : KIND_TABLE) {
		Dictionary def;
		def["strength"] = entry.def.strength;
		def["rough"] = entry.def.rough;
		def["spread"] = entry.def.spread;
		def["metal"] = entry.def.metal;
		def["detail"] = entry.def.detail;
		result[entry.name] = def;
	}
	return result;
}

Dictionary UZUPBRFromImage::make(const Ref<Image> &p_image, const String &p_kind, float p_strength, float p_delight, bool p_tile, bool p_invert, const Ref<Image> &p_normal_from, int p_max_size) {
	if (p_image.is_null() || p_image->is_empty()) {
		return error_result("make needs a non-empty image");
	}
	const KindDef *kind = find_kind(p_kind);
	if (kind == nullptr) {
		return error_result("unknown kind (see kinds())");
	}
	const float delight = CLAMP(p_delight, 0.0f, 1.0f);

	Ref<Image> src = p_image->duplicate();
	if (src->is_compressed()) {
		src->decompress();
	}
	src->convert(Image::FORMAT_RGBF);
	if (p_max_size > 0 && src->get_width() > p_max_size) {
		const int new_h = int(Math::round(double(p_max_size) * src->get_height() / src->get_width()));
		src->resize(p_max_size, new_h, Image::INTERPOLATE_LANCZOS);
	}
	const int w = src->get_width();
	const int h = src->get_height();
	const int count = w * h;
	const PackedByteArray src_data = src->get_data();
	const float *src_px = reinterpret_cast<const float *>(src_data.ptr());

	const float strength = p_strength * kind->strength;
	Array warnings;

	// Luminance and the slow light across it (the baked shading).
	Vector<float> lum;
	lum.resize(count);
	for (int i = 0; i < count; i++) {
		lum.write[i] = 0.2126f * src_px[i * 3] + 0.7152f * src_px[i * 3 + 1] + 0.0722f * src_px[i * 3 + 2];
	}
	const Vector<float> lit = blur_buf(lum, w, h, 64, p_tile);
	double mean_d = 0.0;
	for (int i = 0; i < count; i++) {
		mean_d += lum[i];
	}
	const float mean = float(mean_d / count);
	const float light_range = percentile_buf(lit, 0.95f) - percentile_buf(lit, 0.05f);
	if (light_range > 0.25f) {
		warnings.push_back(String("strong uneven lighting baked into the image (range ") + String::num(light_range, 2) + "); delight is removing it, check the albedo");
	}

	// Albedo without the slow light gradient.
	PackedByteArray albedo_bytes;
	albedo_bytes.resize(count * 3);
	{
		uint8_t *out = albedo_bytes.ptrw();
		for (int i = 0; i < count; i++) {
			const float k = CLAMP(Math::pow(mean / MAXF(lit[i], 0.02f), delight), 0.5f, 2.0f);
			out[i * 3] = to_byte(CLAMP(src_px[i * 3] * k, 0.0f, 1.0f));
			out[i * 3 + 1] = to_byte(CLAMP(src_px[i * 3 + 1] * k, 0.0f, 1.0f));
			out[i * 3 + 2] = to_byte(CLAMP(src_px[i * 3 + 2] * k, 0.0f, 1.0f));
		}
	}
	Ref<Image> albedo = Image::create_from_data(w, h, false, Image::FORMAT_RGB8, albedo_bytes);

	// Height: two scales of detail above the slow light; bright = high.
	const Vector<float> mid = blur_buf(lum, w, h, 6, p_tile);
	Vector<float> height;
	height.resize(count);
	for (int i = 0; i < count; i++) {
		height.write[i] = (lum[i] - lit[i]) * (1.0f - kind->detail) + (lum[i] - mid[i]) * kind->detail;
	}
	if (p_invert) {
		for (int i = 0; i < count; i++) {
			height.write[i] = -height[i];
		}
	}
	height = blur_buf(height, w, h, 2, p_tile);
	{
		const float lo = percentile_buf(height, 0.02f);
		const float hi = percentile_buf(height, 0.98f);
		const float range = MAXF(hi - lo, 1e-4f);
		for (int i = 0; i < count; i++) {
			height.write[i] = CLAMP((height[i] - lo) / range, 0.0f, 1.0f);
		}
	}

	// Normal from height (central differences), OpenGL convention.
	PackedByteArray normal_bytes;
	normal_bytes.resize(count * 3);
	{
		uint8_t *out = normal_bytes.ptrw();
		const float scale = strength * float(w) / 512.0f;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				const int xl = p_tile ? (x - 1 + w) % w : MAX(x - 1, 0);
				const int xr = p_tile ? (x + 1) % w : MIN(x + 1, w - 1);
				const int yu = p_tile ? (y - 1 + h) % h : MAX(y - 1, 0);
				const int yd = p_tile ? (y + 1) % h : MIN(y + 1, h - 1);
				const float dx = (height[y * w + xr] - height[y * w + xl]) * 0.5f * scale;
				const float dy = (height[yd * w + x] - height[yu * w + x]) * 0.5f * scale;
				const Vector3 n = Vector3(-dx, dy, 1.0f).normalized();
				const int i = y * w + x;
				out[i * 3] = to_byte(n.x * 0.5f + 0.5f);
				out[i * 3 + 1] = to_byte(n.y * 0.5f + 0.5f);
				out[i * 3 + 2] = to_byte(n.z * 0.5f + 0.5f);
			}
		}
	}
	Ref<Image> normal = Image::create_from_data(w, h, false, Image::FORMAT_RGB8, normal_bytes);
	String normal_source = "brightness";
	if (p_normal_from.is_valid() && !p_normal_from->is_empty()) {
		Ref<Image> given = p_normal_from->duplicate();
		if (given->is_compressed()) {
			given->decompress();
		}
		given->convert(Image::FORMAT_RGB8);
		given->resize(w, h, Image::INTERPOLATE_BILINEAR);
		normal = given;
		normal_source = "given";
	}

	// ORM: AO from how far below its surroundings a point sits (kept gentle);
	// roughness per kind, rougher in the cavities; metallic from the kind.
	const Vector<float> around = blur_buf(height, w, h, 8, p_tile);
	PackedByteArray orm_bytes;
	orm_bytes.resize(count * 3);
	double rough_sum = 0.0;
	{
		uint8_t *out = orm_bytes.ptrw();
		for (int i = 0; i < count; i++) {
			const float cavity = MAXF(0.0f, around[i] - height[i]);
			const float ao = CLAMP(1.0f - cavity * 1.6f, 0.35f, 1.0f);
			const float r = CLAMP(kind->rough + kind->spread * (cavity * 2.0f - (height[i] - 0.5f) * 0.6f), 0.04f, 1.0f);
			rough_sum += r;
			out[i * 3] = to_byte(ao);
			out[i * 3 + 1] = to_byte(r);
			out[i * 3 + 2] = to_byte(kind->metal);
		}
	}
	Ref<Image> orm = Image::create_from_data(w, h, false, Image::FORMAT_RGB8, orm_bytes);

	PackedByteArray height_bytes;
	height_bytes.resize(count);
	{
		uint8_t *out = height_bytes.ptrw();
		for (int i = 0; i < count; i++) {
			out[i] = to_byte(height[i]);
		}
	}
	Ref<Image> height_img = Image::create_from_data(w, h, false, Image::FORMAT_L8, height_bytes);

	// Seam check: how different the opposite edges are (0 = tiles seamlessly).
	float seam = 0.0f;
	for (int y = 0; y < h; y++) {
		seam += Math::absf(lum[y * w] - lum[y * w + w - 1]);
	}
	for (int x = 0; x < w; x++) {
		seam += Math::absf(lum[x] - lum[(h - 1) * w + x]);
	}
	seam /= float(w + h);
	if (seam > 0.08f && !p_tile) {
		warnings.push_back(String("edges do not match (seam ") + String::num(seam, 2) + "): the texture will show seams when repeated");
	}

	Dictionary stats;
	stats["roughness_mean"] = float(Math::snapped(double(float(rough_sum / count)), 0.01));
	stats["normal_strength"] = float(Math::snapped(double(strength), 0.01));
	stats["baked_light_range"] = float(Math::snapped(double(light_range), 0.01));
	stats["seam"] = float(Math::snapped(double(seam), 0.01));

	Array size;
	size.push_back(w);
	size.push_back(h);

	Dictionary result;
	result["ok"] = true;
	result["kind"] = p_kind;
	result["size"] = size;
	result["albedo"] = albedo;
	result["normal"] = normal;
	result["orm"] = orm;
	result["height"] = height_img;
	result["normal_source"] = normal_source;
	result["stats"] = stats;
	result["warnings"] = warnings;
	result["note"] = "estimated maps (not a scan); check with a preview under a raking light";
	return result;
}

Dictionary UZUPBRFromImage::make_file(const String &p_in_path, const String &p_out_dir, const String &p_kind, float p_strength, float p_delight, bool p_tile, bool p_invert, const String &p_normal_from_path, int p_max_size) {
	const Ref<Image> src = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(p_in_path));
	if (src.is_null() || src->is_empty()) {
		return error_result("cannot read " + p_in_path);
	}
	Ref<Image> normal_from;
	if (!p_normal_from_path.is_empty()) {
		normal_from = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(p_normal_from_path));
		if (normal_from.is_null() || normal_from->is_empty()) {
			return error_result("cannot read --normal-from " + p_normal_from_path);
		}
	}
	Dictionary result = make(src, p_kind, p_strength, p_delight, p_tile, p_invert, normal_from, p_max_size);
	if (!bool(result["ok"])) {
		return result;
	}
	const String out_abs = ProjectSettings::get_singleton()->globalize_path(p_out_dir);
	DirAccess::make_dir_recursive_absolute(out_abs);

	Ref<Image> albedo = result["albedo"];
	Ref<Image> normal = result["normal"];
	Ref<Image> orm = result["orm"];
	Ref<Image> height_img = result["height"];
	Dictionary files;
	files["albedo"] = p_out_dir + "/albedo.png";
	files["normal"] = p_out_dir + "/normal.png";
	files["orm"] = p_out_dir + "/orm.png";
	files["height"] = p_out_dir + "/height.png";
	albedo->save_png(out_abs + "/albedo.png");
	normal->save_png(out_abs + "/normal.png");
	orm->save_png(out_abs + "/orm.png");
	height_img->save_png(out_abs + "/height.png");

	// A material that uses the maps (textures embedded, no import step needed).
	Ref<ORMMaterial3D> mat;
	mat.instantiate();
	mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(albedo));
	mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, ImageTexture::create_from_image(normal));
	mat->set_texture(BaseMaterial3D::TEXTURE_ORM, ImageTexture::create_from_image(orm));
	files["material"] = p_out_dir + "/material.tres";
	ResourceSaver::save(mat, out_abs + "/material.tres");

	result.erase("albedo");
	result.erase("normal");
	result.erase("orm");
	result.erase("height");
	result["files"] = files;
	return result;
}

Dictionary UZUPBRFromImage::compare(const Ref<Image> &p_normal, const Ref<Image> &p_ref, const Ref<Image> &p_rough, const Ref<Image> &p_ref_rough) {
	if (p_normal.is_null() || p_normal->is_empty() || p_ref.is_null() || p_ref->is_empty()) {
		return error_result("compare needs --normal and --ref");
	}
	const int n = 256;
	Ref<Image> a = p_normal->duplicate();
	Ref<Image> b = p_ref->duplicate();
	for (Ref<Image> img : { a, b }) {
		if (img->is_compressed()) {
			img->decompress();
		}
		img->convert(Image::FORMAT_RGB8);
		img->resize(n, n, Image::INTERPOLATE_BILINEAR);
	}
	const PackedByteArray a_data = a->get_data();
	const PackedByteArray b_data = b->get_data();
	double ang = 0.0;
	double ang_flat = 0.0;
	for (int i = 0; i < n * n; i++) {
		const Vector3 va(a_data[i * 3] / 255.0f * 2.0f - 1.0f, a_data[i * 3 + 1] / 255.0f * 2.0f - 1.0f, a_data[i * 3 + 2] / 255.0f * 2.0f - 1.0f);
		const Vector3 vb(b_data[i * 3] / 255.0f * 2.0f - 1.0f, b_data[i * 3 + 1] / 255.0f * 2.0f - 1.0f, b_data[i * 3 + 2] / 255.0f * 2.0f - 1.0f);
		ang += Math::rad_to_deg(va.normalized().angle_to(vb.normalized()));
		ang_flat += Math::rad_to_deg(Vector3(0, 0, 1).angle_to(vb.normalized()));
	}
	Dictionary result;
	result["ok"] = true;
	result["normal_angle_error_deg"] = float(Math::snapped(double(float(ang / (n * n))), 0.01));
	result["flat_normal_error_deg"] = float(Math::snapped(double(float(ang_flat / (n * n))), 0.01));

	if (p_rough.is_valid() && !p_rough->is_empty() && p_ref_rough.is_valid() && !p_ref_rough->is_empty()) {
		Ref<Image> ia = p_rough->duplicate();
		Ref<Image> ib = p_ref_rough->duplicate();
		for (Ref<Image> img : { ia, ib }) {
			if (img->is_compressed()) {
				img->decompress();
			}
			img->convert(Image::FORMAT_RGB8);
			img->resize(n, n, Image::INTERPOLATE_BILINEAR);
		}
		const PackedByteArray ga_data = ia->get_data();
		const PackedByteArray gb_data = ib->get_data();
		Vector<float> ga;
		Vector<float> gb;
		ga.resize(n * n);
		gb.resize(n * n);
		double ma = 0.0;
		double mb = 0.0;
		for (int i = 0; i < n * n; i++) {
			ga.write[i] = ga_data[i * 3 + 1] / 255.0f;
			gb.write[i] = gb_data[i * 3 + 1] / 255.0f;
			ma += ga[i];
			mb += gb[i];
		}
		ma /= (n * n);
		mb /= (n * n);
		double cov = 0.0;
		double va = 0.0;
		double vb = 0.0;
		for (int i = 0; i < n * n; i++) {
			cov += (ga[i] - ma) * (gb[i] - mb);
			va += (ga[i] - ma) * (ga[i] - ma);
			vb += (gb[i] - mb) * (gb[i] - mb);
		}
		result["roughness_mean"] = float(Math::snapped(double(float(ma)), 0.01));
		result["ref_roughness_mean"] = float(Math::snapped(double(float(mb)), 0.01));
		result["roughness_correlation"] = float(Math::snapped(cov / Math::sqrt(MAXF(va * vb, 1e-9)), 0.01));
	}
	return result;
}

void UZUPBRFromImage::_bind_methods() {
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("kinds"), &UZUPBRFromImage::kinds);
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("make", "image", "kind", "strength", "delight", "tile", "invert", "normal_from", "max_size"), &UZUPBRFromImage::make, DEFVAL(1.0), DEFVAL(0.7), DEFVAL(false), DEFVAL(false), DEFVAL(Ref<Image>()), DEFVAL(1024));
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("make_file", "in_path", "out_dir", "kind", "strength", "delight", "tile", "invert", "normal_from_path", "max_size"), &UZUPBRFromImage::make_file, DEFVAL(1.0), DEFVAL(0.7), DEFVAL(false), DEFVAL(false), DEFVAL(String()), DEFVAL(1024));
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("compare", "normal", "ref", "rough", "ref_rough"), &UZUPBRFromImage::compare, DEFVAL(Ref<Image>()), DEFVAL(Ref<Image>()));
}
