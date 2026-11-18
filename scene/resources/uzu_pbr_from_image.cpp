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

#include "uzu_pbr_from_image.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"

namespace {

struct Kind {
	const char *name;
	float strength; // Normal strength.
	float rough; // Roughness base,
	float spread; // and how much it varies.
	float metal;
	float detail; // Height: the share of fine grain vs larger shapes.
};

// Fitted 2026-11-18 against Poly Haven scans (brick, cobblestone, rock) in the prototype.
const Kind KINDS[] = {
	{ "stone", 0.55, 0.82, 0.18, 0.0, 0.5 },
	{ "brick", 0.65, 0.86, 0.14, 0.0, 0.4 },
	{ "cobble", 0.8, 0.62, 0.22, 0.0, 0.4 },
	{ "ground", 0.45, 0.9, 0.1, 0.0, 0.6 },
	{ "wood", 0.3, 0.7, 0.2, 0.0, 0.7 },
	{ "plaster", 0.25, 0.88, 0.08, 0.0, 0.6 },
	{ "fabric", 0.2, 0.95, 0.05, 0.0, 0.9 },
	{ "leather", 0.25, 0.6, 0.2, 0.0, 0.8 },
	{ "metal", 0.2, 0.4, 0.35, 1.0, 0.7 },
	{ "painted_metal", 0.2, 0.55, 0.3, 0.0, 0.7 },
};

const Kind *find_kind(const String &p_name) {
	for (const Kind &k : KINDS) {
		if (p_name == k.name) {
			return &k;
		}
	}
	return nullptr;
}

Vector<float> luminance(const Ref<Image> &p_image) {
	const int w = p_image->get_width();
	const int h = p_image->get_height();
	Vector<float> out;
	out.resize(w * h);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const Color c = p_image->get_pixel(x, y);
			out.write[y * w + x] = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
		}
	}
	return out;
}

Ref<Image> to_image(const Vector<float> &p_buf, int p_w, int p_h) {
	Vector<uint8_t> bytes;
	bytes.resize(p_buf.size() * sizeof(float));
	memcpy(bytes.ptrw(), p_buf.ptr(), bytes.size());
	return Image::create_from_data(p_w, p_h, false, Image::FORMAT_RF, bytes);
}

Vector<float> from_image(const Ref<Image> &p_image) {
	const Vector<uint8_t> bytes = p_image->get_data();
	Vector<float> out;
	out.resize(bytes.size() / sizeof(float));
	memcpy(out.ptrw(), bytes.ptr(), out.size() * sizeof(float));
	return out;
}

// The mean of each block when shrinking p_w x p_h to p_tw x p_th. (A bilinear resize
// would pick single pixels: on a regular texture they can all land on mortar lines.)
Ref<Image> shrink(const Vector<float> &p_buf, int p_w, int p_h, int p_tw, int p_th) {
	Vector<float> out;
	out.resize(p_tw * p_th);
	for (int ty = 0; ty < p_th; ty++) {
		const int y0 = ty * p_h / p_th;
		const int y1 = MAX(y0 + 1, (ty + 1) * p_h / p_th);
		for (int tx = 0; tx < p_tw; tx++) {
			const int x0 = tx * p_w / p_tw;
			const int x1 = MAX(x0 + 1, (tx + 1) * p_w / p_tw);
			double sum = 0.0;
			for (int y = y0; y < y1; y++) {
				for (int x = x0; x < x1; x++) {
					sum += p_buf[y * p_w + x];
				}
			}
			out.write[ty * p_tw + tx] = sum / ((y1 - y0) * (x1 - x0));
		}
	}
	return to_image(out, p_tw, p_th);
}

// Blur by shrinking (block means) and growing again (cubic); p_factor = how many times
// smaller. A tiling image is padded with wrapped copies, so the blur wraps around the
// edges too.
Vector<float> blur(const Vector<float> &p_buf, int p_w, int p_h, int p_factor, bool p_tile) {
	Ref<Image> img;
	if (p_tile) {
		Vector<float> big;
		big.resize(p_w * 3 * p_h * 3);
		for (int y = 0; y < p_h * 3; y++) {
			for (int x = 0; x < p_w * 3; x++) {
				big.write[y * p_w * 3 + x] = p_buf[(y % p_h) * p_w + x % p_w];
			}
		}
		img = shrink(big, p_w * 3, p_h * 3, MAX(3, p_w * 3 / p_factor), MAX(3, p_h * 3 / p_factor));
		img->resize(p_w * 3, p_h * 3, Image::INTERPOLATE_CUBIC);
		img = img->get_region(Rect2i(p_w, p_h, p_w, p_h));
	} else {
		img = shrink(p_buf, p_w, p_h, MAX(2, p_w / p_factor), MAX(2, p_h / p_factor));
		img->resize(p_w, p_h, Image::INTERPOLATE_CUBIC);
	}
	return from_image(img);
}

float percentile(const Vector<float> &p_buf, float p_p) {
	Vector<float> s = p_buf;
	s.sort();
	return s[CLAMP(int(s.size() * p_p), 0, s.size() - 1)];
}

double snap(double p_value) {
	return Math::snapped(p_value, 0.01);
}

Dictionary failure(const String &p_error) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	return result;
}

} // namespace

Dictionary UZUPBRFromImage::get_kinds() {
	Dictionary kinds;
	for (const Kind &k : KINDS) {
		Dictionary d;
		d["strength"] = k.strength;
		d["rough"] = k.rough;
		d["spread"] = k.spread;
		d["metal"] = k.metal;
		d["detail"] = k.detail;
		kinds[k.name] = d;
	}
	return kinds;
}

Dictionary UZUPBRFromImage::make(const Ref<Image> &p_image, const String &p_kind, const Dictionary &p_options) {
	if (p_image.is_null() || p_image->is_empty()) {
		return failure("make needs an image");
	}
	const Kind *kind = find_kind(p_kind);
	if (kind == nullptr) {
		return failure(vformat("unknown kind '%s' (see get_kinds)", p_kind));
	}
	Ref<Image> src = p_image->duplicate();
	if (src->is_compressed()) {
		src->decompress();
	}
	src->convert(Image::FORMAT_RGBF);
	const int size = p_options.get("size", 1024);
	if (size > 0 && src->get_width() > size) {
		src->resize(size, int(Math::round(size * double(src->get_height()) / src->get_width())), Image::INTERPOLATE_LANCZOS);
	}
	const int w = src->get_width();
	const int h = src->get_height();
	const int n = w * h;
	const bool tile = p_options.get("tile", false);
	const float strength = float(p_options.get("strength", 1.0)) * kind->strength;
	const float delight = CLAMP(float(p_options.get("delight", 0.7)), 0.0f, 1.0f);
	Array warnings;

	const Vector<float> lum = luminance(src);
	const Vector<float> lit = blur(lum, w, h, 64, tile); // The slow light across the image = baked shading.
	double mean = 0.0;
	for (int i = 0; i < n; i++) {
		mean += lum[i];
	}
	mean /= n;
	const float light_range = percentile(lit, 0.95) - percentile(lit, 0.05);
	if (light_range > 0.25) {
		warnings.push_back(vformat("strong uneven lighting baked into the image (range %.2f); delight is removing it, check albedo.png", light_range));
	}

	// Albedo without the slow light gradient.
	Ref<Image> albedo = Image::create_empty(w, h, false, Image::FORMAT_RGB8);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const double k = Math::pow(mean / MAX(double(lit[y * w + x]), 0.02), double(delight));
			const Color c = src->get_pixel(x, y) * CLAMP(float(k), 0.5f, 2.0f);
			albedo->set_pixel(x, y, Color(CLAMP(c.r, 0.0f, 1.0f), CLAMP(c.g, 0.0f, 1.0f), CLAMP(c.b, 0.0f, 1.0f)));
		}
	}

	// Height: the detail above the slow light, at two scales (shapes, and fine grain),
	// a little smoothed; bright = high (mortar and cracks are dark).
	const Vector<float> mid = blur(lum, w, h, 6, tile);
	Vector<float> height;
	height.resize(n);
	const float sign = bool(p_options.get("invert", false)) ? -1.0f : 1.0f;
	for (int i = 0; i < n; i++) {
		height.write[i] = sign * ((lum[i] - lit[i]) * (1.0f - kind->detail) + (lum[i] - mid[i]) * kind->detail);
	}
	height = blur(height, w, h, 2, tile);
	const float lo = percentile(height, 0.02);
	const float hi = percentile(height, 0.98);
	for (int i = 0; i < n; i++) {
		height.write[i] = CLAMP((height[i] - lo) / MAX(hi - lo, 1e-4f), 0.0f, 1.0f);
	}

	// Normal from height (central differences), OpenGL: +Y (green) is up in the image.
	Ref<Image> normal = Image::create_empty(w, h, false, Image::FORMAT_RGB8);
	const float scale = strength * float(w) / 512.0f;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int xl = tile ? (x - 1 + w) % w : MAX(x - 1, 0);
			const int xr = tile ? (x + 1) % w : MIN(x + 1, w - 1);
			const int yu = tile ? (y - 1 + h) % h : MAX(y - 1, 0);
			const int yd = tile ? (y + 1) % h : MIN(y + 1, h - 1);
			const float dx = (height[y * w + xr] - height[y * w + xl]) * 0.5f * scale;
			const float dy = (height[yd * w + x] - height[yu * w + x]) * 0.5f * scale;
			const Vector3 v = Vector3(-dx, dy, 1.0).normalized();
			normal->set_pixel(x, y, Color(v.x * 0.5 + 0.5, v.y * 0.5 + 0.5, v.z * 0.5 + 0.5));
		}
	}
	// A normal map from an outside model (brought in with UZUMapImport first, so its
	// convention, size and offset are already checked) replaces the brightness-based one.
	String normal_source = "brightness";
	const Ref<Image> given = p_options.get("normal", Ref<Image>());
	if (given.is_valid() && !given->is_empty()) {
		normal = given->duplicate();
		if (normal->is_compressed()) {
			normal->decompress();
		}
		normal->convert(Image::FORMAT_RGB8);
		normal->resize(w, h, Image::INTERPOLATE_BILINEAR);
		normal_source = p_options.get("normal_source", "image");
	}

	// AO: how far below its surroundings each point sits (kept gentle). Roughness: per
	// kind, rougher in cavities and on low ground.
	const Vector<float> around = blur(height, w, h, 8, tile);
	Ref<Image> orm = Image::create_empty(w, h, false, Image::FORMAT_RGB8);
	Ref<Image> height_image = Image::create_empty(w, h, false, Image::FORMAT_L8);
	double rough_sum = 0.0;
	for (int i = 0; i < n; i++) {
		const float cavity = MAX(0.0f, around[i] - height[i]);
		const float ao = CLAMP(1.0f - cavity * 1.6f, 0.35f, 1.0f);
		const float r = CLAMP(kind->rough + kind->spread * (cavity * 2.0f - (height[i] - 0.5f) * 0.6f), 0.04f, 1.0f);
		rough_sum += r;
		orm->set_pixel(i % w, i / w, Color(ao, r, kind->metal));
		height_image->set_pixel(i % w, i / w, Color(height[i], height[i], height[i]));
	}

	// Seam check: how different the left/right and top/bottom edges are (0 = tiles seamlessly).
	double seam = 0.0;
	for (int y = 0; y < h; y++) {
		seam += Math::abs(lum[y * w] - lum[y * w + w - 1]);
	}
	for (int x = 0; x < w; x++) {
		seam += Math::abs(lum[x] - lum[(h - 1) * w + x]);
	}
	seam /= (w + h);
	if (seam > 0.08 && !tile) {
		warnings.push_back(vformat("edges do not match (seam %.2f): the texture will show seams when repeated", seam));
	}

	Ref<ORMMaterial3D> material;
	material.instantiate();
	material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(albedo));
	material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, ImageTexture::create_from_image(normal));
	material->set_texture(BaseMaterial3D::TEXTURE_ORM, ImageTexture::create_from_image(orm));

	Dictionary stats;
	stats["roughness_mean"] = snap(rough_sum / n);
	stats["normal_strength"] = snap(strength);
	stats["baked_light_range"] = snap(light_range);
	stats["seam"] = snap(seam);
	Dictionary result;
	result["ok"] = true;
	result["kind"] = p_kind;
	result["size"] = Array{ w, h };
	result["albedo"] = albedo;
	result["normal"] = normal;
	result["orm"] = orm;
	result["height"] = height_image;
	result["material"] = material;
	result["normal_source"] = normal_source;
	result["stats"] = stats;
	result["warnings"] = warnings;
	result["note"] = "estimated maps (not a scan); check them under a raking light";
	return result;
}

Dictionary UZUPBRFromImage::make_file(const String &p_in_path, const String &p_out_dir, const String &p_kind, const Dictionary &p_options) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (p_in_path.is_empty() || p_out_dir.is_empty()) {
		return failure("make needs an input image and an output folder");
	}
	const Ref<Image> src = Image::load_from_file(ps->globalize_path(p_in_path));
	if (src.is_null()) {
		return failure(vformat("cannot read %s", p_in_path));
	}
	Dictionary options = p_options.duplicate();
	const String normal_from = options.get("normal_from", "");
	if (!normal_from.is_empty()) {
		const Ref<Image> given = Image::load_from_file(ps->globalize_path(normal_from));
		if (given.is_null()) {
			return failure(vformat("cannot read the normal map %s", normal_from));
		}
		options["normal"] = given;
		options["normal_source"] = normal_from;
	}
	Dictionary result = make(src, p_kind, options);
	if (!bool(result["ok"])) {
		return result;
	}
	const String out_dir = p_out_dir.trim_suffix("/");
	DirAccess::make_dir_recursive_absolute(ps->globalize_path(out_dir));
	Dictionary files;
	for (const char *name : { "albedo", "normal", "orm", "height" }) {
		const String path = out_dir + "/" + name + ".png";
		const Ref<Image> image = result[name];
		const Error err = image->save_png(ps->globalize_path(path));
		if (err != OK) {
			return failure(vformat("could not save %s: %s", path, error_names[err]));
		}
		files[name] = path;
		result.erase(name);
	}
	// The material keeps its textures inside (it works without an import step).
	const String material_path = out_dir + "/material.tres";
	const Error err = ResourceSaver::save(result["material"], material_path);
	if (err != OK) {
		return failure(vformat("could not save %s: %s", material_path, error_names[err]));
	}
	files["material"] = material_path;
	result.erase("material");
	result["files"] = files;
	return result;
}

Dictionary UZUPBRFromImage::compare(const Ref<Image> &p_normal, const Ref<Image> &p_reference, const Ref<Image> &p_orm, const Ref<Image> &p_reference_orm) {
	if (p_normal.is_null() || p_reference.is_null() || p_normal->is_empty() || p_reference->is_empty()) {
		return failure("compare needs a normal map and a reference");
	}
	const int n = 256;
	auto prepared = [](const Ref<Image> &p_src) {
		Ref<Image> img = p_src->duplicate();
		if (img->is_compressed()) {
			img->decompress();
		}
		img->convert(Image::FORMAT_RGBAF);
		img->resize(n, n, Image::INTERPOLATE_BILINEAR);
		return img;
	};
	const Ref<Image> a = prepared(p_normal);
	const Ref<Image> b = prepared(p_reference);
	double angle = 0.0;
	double angle_flat = 0.0;
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) {
			const Color ca = a->get_pixel(x, y);
			const Color cb = b->get_pixel(x, y);
			const Vector3 va = (Vector3(ca.r, ca.g, ca.b) * 2.0 - Vector3(1, 1, 1)).normalized();
			const Vector3 vb = (Vector3(cb.r, cb.g, cb.b) * 2.0 - Vector3(1, 1, 1)).normalized();
			angle += Math::rad_to_deg(va.angle_to(vb));
			angle_flat += Math::rad_to_deg(Vector3(0, 0, 1).angle_to(vb));
		}
	}
	Dictionary result;
	result["ok"] = true;
	result["normal_angle_error_deg"] = snap(angle / (n * n));
	result["flat_normal_error_deg"] = snap(angle_flat / (n * n));
	if (p_orm.is_valid() && p_reference_orm.is_valid() && !p_orm->is_empty() && !p_reference_orm->is_empty()) {
		const Ref<Image> ia = prepared(p_orm);
		const Ref<Image> ib = prepared(p_reference_orm);
		double ma = 0.0;
		double mb = 0.0;
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				ma += ia->get_pixel(x, y).g;
				mb += ib->get_pixel(x, y).g;
			}
		}
		ma /= n * n;
		mb /= n * n;
		double cov = 0.0;
		double va = 0.0;
		double vb = 0.0;
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const double da = ia->get_pixel(x, y).g - ma;
				const double db = ib->get_pixel(x, y).g - mb;
				cov += da * db;
				va += da * da;
				vb += db * db;
			}
		}
		result["roughness_mean"] = snap(ma);
		result["ref_roughness_mean"] = snap(mb);
		result["roughness_correlation"] = snap(cov / Math::sqrt(MAX(va * vb, 1e-9)));
	}
	return result;
}

void UZUPBRFromImage::_bind_methods() {
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("get_kinds"), &UZUPBRFromImage::get_kinds);
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("make", "image", "kind", "options"), &UZUPBRFromImage::make, DEFVAL("stone"), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("make_file", "in_path", "out_dir", "kind", "options"), &UZUPBRFromImage::make_file, DEFVAL("stone"), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUPBRFromImage", D_METHOD("compare", "normal", "reference", "orm", "reference_orm"), &UZUPBRFromImage::compare, DEFVAL(Ref<Image>()), DEFVAL(Ref<Image>()));
}
