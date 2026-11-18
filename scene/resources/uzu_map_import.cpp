/**************************************************************************/
/*  uzu_map_import.cpp                                                    */
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

#include "uzu_map_import.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/object/class_db.h"

namespace {

const int ANALYSIS_WIDTH = 256; // The analysis grid: the picture scaled to this width.
const int SEARCH_CELLS = 6; // The offset search, +- grid cells.

// One channel (0..2) or the luminance (-1) of an image, scaled to w x h.
Vector<float> grid(const Ref<Image> &p_image, int p_w, int p_h, int p_channel) {
	Ref<Image> small = p_image->duplicate();
	small->convert(Image::FORMAT_RGBAF);
	small->resize(p_w, p_h, Image::INTERPOLATE_BILINEAR);
	Vector<float> out;
	out.resize(p_w * p_h);
	for (int y = 0; y < p_h; y++) {
		for (int x = 0; x < p_w; x++) {
			const Color c = small->get_pixel(x, y);
			out.write[y * p_w + x] = p_channel < 0 ? 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b : c[p_channel];
		}
	}
	return out;
}

float correlation(const Vector<float> &p_a, const Vector<float> &p_b) {
	double ma = 0.0;
	double mb = 0.0;
	for (int i = 0; i < p_a.size(); i++) {
		ma += p_a[i];
		mb += p_b[i];
	}
	ma /= MAX(p_a.size(), 1);
	mb /= MAX(p_b.size(), 1);
	double c = 0.0;
	double va = 0.0;
	double vb = 0.0;
	for (int i = 0; i < p_a.size(); i++) {
		c += (p_a[i] - ma) * (p_b[i] - mb);
		va += (p_a[i] - ma) * (p_a[i] - ma);
		vb += (p_b[i] - mb) * (p_b[i] - mb);
	}
	return c / Math::sqrt(MAX(va * vb, 1e-12));
}

// Gradient magnitude (edges), for alignment.
Vector<float> edges(const Vector<float> &p_g, int p_w, int p_h) {
	Vector<float> out;
	out.resize(p_w * p_h);
	out.fill(0.0f);
	for (int y = 1; y < p_h - 1; y++) {
		for (int x = 1; x < p_w - 1; x++) {
			const float dx = p_g[y * p_w + x + 1] - p_g[y * p_w + x - 1];
			const float dy = p_g[(y + 1) * p_w + x] - p_g[(y - 1) * p_w + x];
			out.write[y * p_w + x] = Math::sqrt(dx * dx + dy * dy);
		}
	}
	return out;
}

struct Shift {
	int dx = 0;
	int dy = 0;
	float score = -2.0f;
	float score_at_zero = 0.0f;
};

// The (dx, dy) in grid cells that moves b onto a, searching +- r.
Shift best_shift(const Vector<float> &p_a, const Vector<float> &p_b, int p_w, int p_h, int p_r) {
	Shift best;
	for (int dy = -p_r; dy <= p_r; dy++) {
		for (int dx = -p_r; dx <= p_r; dx++) {
			double s = 0.0;
			int n = 0;
			for (int y = p_r; y < p_h - p_r; y += 2) {
				for (int x = p_r; x < p_w - p_r; x += 2) {
					s += p_a[y * p_w + x] * p_b[(y + dy) * p_w + x + dx];
					n++;
				}
			}
			const float score = s / MAX(n, 1);
			if (dx == 0 && dy == 0) {
				best.score_at_zero = score;
			}
			if (score > best.score) {
				const float zero = best.score_at_zero;
				best = { dx, dy, score, zero };
			}
		}
	}
	return best;
}

void fix_normal(const Ref<Image> &p_image, const Vector<float> &p_lum, int p_w, int p_h, Dictionary &r_found, Array &r_changed, Array &r_warnings) {
	// The picture's own shading: brightness slopes. In an OpenGL normal map, R follows
	// -d(height)/dx and G follows +d(height)/dy (image rows go down). Whether bright means
	// high or low differs per picture, so judge G relative to R: the same relation is
	// OpenGL, the opposite is DirectX (green flipped).
	const Vector<float> r = grid(p_image, p_w, p_h, 0);
	const Vector<float> g = grid(p_image, p_w, p_h, 1);
	const Vector<float> b = grid(p_image, p_w, p_h, 2);
	Vector<float> gx, gy, rr, gg;
	double length_sum = 0.0;
	double z_sum = 0.0;
	for (int y = 1; y < p_h - 1; y++) {
		for (int x = 1; x < p_w - 1; x++) {
			const int i = y * p_w + x;
			gx.push_back(-(p_lum[i + 1] - p_lum[i - 1]));
			gy.push_back(p_lum[i + p_w] - p_lum[i - p_w]);
			rr.push_back(r[i]);
			gg.push_back(g[i]);
			const Vector3 v = Vector3(r[i], g[i], b[i]) * 2.0 - Vector3(1, 1, 1);
			length_sum += v.length();
			z_sum += v.z;
		}
	}
	const double n = MAX(rr.size(), 1);
	const float cx = correlation(rr, gx);
	const float cy = correlation(gg, gy);
	r_found["shading_match_x"] = Math::snapped(cx, 0.01f);
	r_found["shading_match_y"] = Math::snapped(cy, 0.01f);
	r_found["mean_length"] = Math::snapped(float(length_sum / n), 0.01f);
	r_found["mean_z"] = Math::snapped(float(z_sum / n), 0.01f);
	if (z_sum / n < 0.5) {
		r_warnings.push_back(vformat("blue (z) is low on average (%.2f): this may be a world- or object-space normal map, not a tangent-space one", z_sum / n));
	}
	bool flip = false;
	if (Math::abs(cx) < 0.05f || Math::abs(cy) < 0.05f) {
		r_found["convention"] = "unsure";
		r_warnings.push_back("cannot tell OpenGL from DirectX: the picture's shading barely matches the map; the map is kept as is (check it under a raking light)");
	} else if (SIGN(cx) == SIGN(cy)) {
		r_found["convention"] = "opengl";
	} else {
		r_found["convention"] = "directx";
		flip = true;
		r_changed.push_back("green flipped (DirectX -> OpenGL, which Godot expects)");
	}
	if (Math::abs(length_sum / n - 1.0) > 0.03) {
		r_changed.push_back(vformat("vectors normalized (mean length was %.2f)", length_sum / n));
	}
	for (int y = 0; y < p_image->get_height(); y++) {
		for (int x = 0; x < p_image->get_width(); x++) {
			const Color c = p_image->get_pixel(x, y);
			Vector3 v = Vector3(c.r, c.g, c.b) * 2.0 - Vector3(1, 1, 1);
			if (flip) {
				v.y = -v.y;
			}
			v.z = MAX(v.z, 0.0f);
			v = v.length() > 1e-4f ? v.normalized() : Vector3(0, 0, 1);
			p_image->set_pixel(x, y, Color(v.x * 0.5f + 0.5f, v.y * 0.5f + 0.5f, v.z * 0.5f + 0.5f));
		}
	}
}

void fix_depth(const Ref<Image> &p_image, int p_w, int p_h, Dictionary &r_found, Array &r_changed, Array &r_warnings, UZUMapImport::DepthKind p_depth_kind) {
	const Vector<float> g = grid(p_image, p_w, p_h, 0);
	float lo = 1e9f;
	float hi = -1e9f;
	for (float v : g) {
		lo = MIN(lo, v);
		hi = MAX(hi, v);
	}
	// In most scenes the bottom of the picture is nearer than the top (ground vs sky).
	double top = 0.0;
	double bottom = 0.0;
	const int band = MAX(1, p_h / 5);
	for (int y = 0; y < band; y++) {
		for (int x = 0; x < p_w; x++) {
			top += g[y * p_w + x];
			bottom += g[(p_h - 1 - y) * p_w + x];
		}
	}
	top /= band * p_w;
	bottom /= band * p_w;
	const bool white_near = bottom > top;
	r_found["bottom_minus_top"] = Math::snapped(float(bottom - top), 0.01f);
	r_found["white_is"] = white_near ? "near" : "far";
	if (Math::abs(bottom - top) < 0.05 * MAX(hi - lo, 1e-4f)) {
		r_warnings.push_back("cannot tell whether white is near or far (the bottom and the top are alike); give the depth kind");
	}
	bool to_disparity = !white_near;
	if (p_depth_kind == UZUMapImport::DEPTH_DISPARITY) {
		to_disparity = false;
	} else if (p_depth_kind == UZUMapImport::DEPTH_LINEAR) {
		to_disparity = true;
	}
	Array range;
	range.push_back(Math::snapped(lo, 0.001f));
	range.push_back(Math::snapped(hi, 0.001f));
	r_found["range_in"] = range;
	if (to_disparity) {
		r_changed.push_back("inverted so that white = near (what U.ZU Picture reads as disparity)");
	}
	if (hi - lo < 0.5f) {
		r_changed.push_back(vformat("stretched range %.2f..%.2f to 0..1", lo, hi));
	}
	for (int y = 0; y < p_image->get_height(); y++) {
		for (int x = 0; x < p_image->get_width(); x++) {
			float v = (p_image->get_pixel(x, y).r - lo) / MAX(hi - lo, 1e-4f);
			v = CLAMP(v, 0.0f, 1.0f);
			if (to_disparity) {
				v = 1.0f - v;
			}
			p_image->set_pixel(x, y, Color(v, v, v));
		}
	}
}

} // namespace

Dictionary UZUMapImport::import_map(const Ref<Image> &p_map, const Ref<Image> &p_picture, MapKind p_kind, DepthKind p_depth_kind, bool p_align) {
	Dictionary result;
	if (p_map.is_null() || p_picture.is_null() || p_map->is_empty() || p_picture->is_empty()) {
		result["ok"] = false;
		result["error"] = "import needs the map and the picture";
		return result;
	}
	Ref<Image> src = p_map->duplicate();
	Ref<Image> pic = p_picture->duplicate();
	if (src->is_compressed()) {
		src->decompress();
	}
	if (pic->is_compressed()) {
		pic->decompress();
	}
	Dictionary found;
	Array changed;
	Array warnings;
	const int pw = pic->get_width();
	const int ph = pic->get_height();
	const float aspect_in = float(src->get_width()) / src->get_height();
	const float aspect_pic = float(pw) / ph;
	Array size_in;
	size_in.push_back(src->get_width());
	size_in.push_back(src->get_height());
	found["size_in"] = size_in;
	if (Math::abs(aspect_in / aspect_pic - 1.0f) > 0.01f) {
		warnings.push_back(vformat("aspect differs (map %.3f, picture %.3f): the model may have cropped or padded; check the alignment", aspect_in, aspect_pic));
	}
	const int w = ANALYSIS_WIDTH;
	const int h = MAX(16, int(Math::round(ANALYSIS_WIDTH / aspect_pic)));
	const Vector<float> lum = grid(pic, w, h, -1);

	// The offset against the picture (models sometimes shift by a few pixels after resizing).
	Vector<float> signal;
	if (p_kind == MAP_DEPTH) {
		signal = grid(src, w, h, 0);
	} else {
		const Vector<float> r = grid(src, w, h, 0);
		const Vector<float> g = grid(src, w, h, 1);
		signal.resize(w * h);
		for (int i = 0; i < w * h; i++) {
			signal.write[i] = r[i] + g[i];
		}
	}
	int shift_x = 0;
	int shift_y = 0;
	if (p_align) {
		const Shift s = best_shift(edges(lum, w, h), edges(signal, w, h), w, h, SEARCH_CELLS);
		Array offset;
		offset.push_back(int(Math::round(s.dx * float(pw) / w)));
		offset.push_back(int(Math::round(s.dy * float(ph) / h)));
		found["offset_px"] = offset;
		found["offset_confidence"] = Math::snapped((s.score - s.score_at_zero) / MAX(Math::abs(s.score_at_zero), 1e-6f), 0.01f);
		if ((s.dx != 0 || s.dy != 0) && s.score > s.score_at_zero * 1.05f) {
			shift_x = s.dx;
			shift_y = s.dy;
		}
	}

	Ref<Image> img = src->duplicate();
	img->convert(Image::FORMAT_RGBAF);
	img->resize(pw, ph, Image::INTERPOLATE_BILINEAR);
	if (img->get_size() != src->get_size()) {
		changed.push_back(vformat("resized %dx%d -> %dx%d", src->get_width(), src->get_height(), pw, ph));
	}
	if (shift_x != 0 || shift_y != 0) {
		const int ox = int(Math::round(shift_x * float(pw) / w));
		const int oy = int(Math::round(shift_y * float(ph) / h));
		Ref<Image> moved = Image::create_empty(pw, ph, false, Image::FORMAT_RGBAF);
		for (int y = 0; y < ph; y++) {
			for (int x = 0; x < pw; x++) {
				moved->set_pixel(x, y, img->get_pixel(CLAMP(x + ox, 0, pw - 1), CLAMP(y + oy, 0, ph - 1)));
			}
		}
		img = moved;
		changed.push_back(vformat("moved by %d, %d px to line up with the picture", -ox, -oy));
	}

	if (p_kind == MAP_NORMAL) {
		fix_normal(img, lum, w, h, found, changed, warnings);
	} else {
		fix_depth(img, w, h, found, changed, warnings, p_depth_kind);
	}
	img->convert(p_kind == MAP_NORMAL ? Image::FORMAT_RGB8 : Image::FORMAT_L8);

	result["ok"] = true;
	result["kind"] = p_kind == MAP_NORMAL ? "normal" : "depth";
	result["image"] = img;
	result["found"] = found;
	result["changed"] = changed;
	result["warnings"] = warnings;
	return result;
}

Dictionary UZUMapImport::import_file(const String &p_map_path, const String &p_picture_path, const String &p_out_path, MapKind p_kind, DepthKind p_depth_kind, bool p_align) {
	Dictionary result;
	result["ok"] = false;
	const Ref<Image> map = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(p_map_path));
	const Ref<Image> picture = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(p_picture_path));
	if (map.is_null() || picture.is_null() || p_out_path.is_empty()) {
		result["error"] = "import needs a readable map, a readable picture and an output path";
		return result;
	}
	result = import_map(map, picture, p_kind, p_depth_kind, p_align);
	if (!bool(result["ok"])) {
		return result;
	}
	const Ref<Image> fixed = result["image"];
	result.erase("image");
	const String out = ProjectSettings::get_singleton()->globalize_path(p_out_path);
	DirAccess::make_dir_recursive_absolute(out.get_base_dir());
	const Error err = fixed->save_png(out);
	if (err != OK) {
		result["ok"] = false;
		result["error"] = vformat("could not save %s: %s", p_out_path, error_names[err]);
		return result;
	}
	result["out"] = p_out_path;
	return result;
}

void UZUMapImport::_bind_methods() {
	ClassDB::bind_static_method("UZUMapImport", D_METHOD("import_map", "map", "picture", "kind", "depth_kind", "align"), &UZUMapImport::import_map, DEFVAL(DEPTH_AUTO), DEFVAL(true));
	ClassDB::bind_static_method("UZUMapImport", D_METHOD("import_file", "map_path", "picture_path", "out_path", "kind", "depth_kind", "align"), &UZUMapImport::import_file, DEFVAL(DEPTH_AUTO), DEFVAL(true));

	BIND_ENUM_CONSTANT(MAP_NORMAL);
	BIND_ENUM_CONSTANT(MAP_DEPTH);
	BIND_ENUM_CONSTANT(DEPTH_AUTO);
	BIND_ENUM_CONSTANT(DEPTH_DISPARITY);
	BIND_ENUM_CONSTANT(DEPTH_LINEAR);
}
