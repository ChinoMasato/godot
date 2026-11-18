/**************************************************************************/
/*  test_uzu_map_import.cpp                                               */
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

TEST_FORCE_LINK(test_uzu_map_import)

#include "core/object/class_db.h"
#include "scene/resources/uzu_map_import.h"

namespace TestUZUMapImport {

// A known surface like a cobbled floor: rounded stones between mortar lines, so the
// picture and the maps share sharp edges (what alignment relies on, as with real scans).
// The picture is the height as brightness (tops lit); the true normal map is OpenGL
// (R = -dh/dx, G = +dh/dy, rows go down).
const int W = 1024;
const int H = 768;

static float height(float x, float y) {
	const float cell_w = 64.0f;
	const float cell_h = 48.0f;
	const int row = int(Math::floor(y / cell_h));
	const float xx = x + (row % 2 ? cell_w * 0.5f : 0.0f);
	const float lx = Math::fposmod(xx, cell_w);
	const float ly = Math::fposmod(y, cell_h);
	const float d = MIN(MIN(lx, cell_w - lx), MIN(ly, cell_h - ly)); // Distance to the mortar.
	const float t = CLAMP(d / 10.0f, 0.0f, 1.0f);
	const float dome = t * t * (3.0f - 2.0f * t);
	return 0.1f + dome * (0.6f + 0.15f * Math::sin(x * 0.05f) * Math::cos(y * 0.07f));
}

static Ref<Image> picture() {
	Ref<Image> image = Image::create_empty(W, H, false, Image::FORMAT_RGB8);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			const float b = CLAMP(height(x, y), 0.0f, 1.0f);
			image->set_pixel(x, y, Color(b, b * 0.9f, b * 0.8f));
		}
	}
	return image;
}

// The normal map, shifted by (sx, sy) px, with green flipped for DirectX, scaled in length.
static Ref<Image> normal_map(bool p_directx, int p_shift_x = 0, float p_length = 1.0f) {
	Ref<Image> image = Image::create_empty(W, H, false, Image::FORMAT_RGB8);
	const float k = 60.0f;
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			const float px = x + p_shift_x;
			const float dx = (height(px + 1, y) - height(px - 1, y)) * 0.5f * k;
			const float dy = (height(px, y + 1) - height(px, y - 1)) * 0.5f * k;
			Vector3 n = Vector3(-dx, p_directx ? -dy : dy, 1.0f).normalized() * p_length;
			image->set_pixel(x, y, Color(n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f));
		}
	}
	return image;
}

// Mean absolute difference of two RGB images in 0..255 steps, away from the borders.
static float difference(const Ref<Image> &p_a, const Ref<Image> &p_b, int p_margin = 40) {
	double sum = 0.0;
	int n = 0;
	for (int y = p_margin; y < H - p_margin; y += 3) {
		for (int x = p_margin; x < W - p_margin; x += 3) {
			const Color a = p_a->get_pixel(x, y);
			const Color b = p_b->get_pixel(x, y);
			sum += (Math::abs(a.r - b.r) + Math::abs(a.g - b.g) + Math::abs(a.b - b.b)) / 3.0f * 255.0f;
			n++;
		}
	}
	return sum / n;
}

TEST_CASE("[UZUMapImport] Registered for scripts") {
	CHECK(ClassDB::class_exists("UZUMapImport"));
	CHECK(ClassDB::has_method("UZUMapImport", "import_map"));
	CHECK(ClassDB::has_method("UZUMapImport", "import_file"));
	const Dictionary bad = UZUMapImport::import_map(Ref<Image>(), picture(), UZUMapImport::MAP_NORMAL);
	CHECK_FALSE(bool(bad["ok"]));
}

TEST_CASE("[UZUMapImport] Normal maps: OpenGL is kept, DirectX is flipped, length is fixed") {
	const Ref<Image> pic = picture();
	const Ref<Image> truth = normal_map(false);

	// Alignment is tested on its own below; here the maps are already in place.
	Dictionary r = UZUMapImport::import_map(truth, pic, UZUMapImport::MAP_NORMAL, UZUMapImport::DEPTH_AUTO, false);
	REQUIRE(bool(r["ok"]));
	CHECK(String(Dictionary(r["found"])["convention"]) == "opengl");
	CHECK(difference(r["image"], truth) < 1.5);

	r = UZUMapImport::import_map(normal_map(true), pic, UZUMapImport::MAP_NORMAL, UZUMapImport::DEPTH_AUTO, false);
	CHECK(String(Dictionary(r["found"])["convention"]) == "directx");
	bool flipped = false;
	for (const Variant &c : Array(r["changed"])) {
		flipped = flipped || String(c).contains("green flipped");
	}
	CHECK(flipped);
	CHECK(difference(r["image"], truth) < 1.5); // Back to the OpenGL truth.

	r = UZUMapImport::import_map(normal_map(false, 0, 0.8), pic, UZUMapImport::MAP_NORMAL, UZUMapImport::DEPTH_AUTO, false);
	bool normalized = false;
	for (const Variant &c : Array(r["changed"])) {
		normalized = normalized || String(c).contains("normalized");
	}
	CHECK(normalized);
	CHECK(difference(r["image"], truth) < 1.5);
}

TEST_CASE("[UZUMapImport] An offset map is moved back onto the picture") {
	// A depth map of the same relief whose content sits 24 px to the left: the picture's
	// edges and the map's edges are the same mortar lines, as with a model's output.
	const Ref<Image> pic = picture();
	auto depth_map = [](int p_shift) {
		Ref<Image> map = Image::create_empty(W, H, false, Image::FORMAT_L8);
		for (int y = 0; y < H; y++) {
			for (int x = 0; x < W; x++) {
				const float d = CLAMP(height(x + p_shift, y), 0.0f, 1.0f);
				map->set_pixel(x, y, Color(d, d, d));
			}
		}
		return map;
	};
	const Ref<Image> truth = depth_map(0);
	Dictionary r = UZUMapImport::import_map(depth_map(24), pic, UZUMapImport::MAP_DEPTH, UZUMapImport::DEPTH_DISPARITY);
	Array offset = Dictionary(r["found"])["offset_px"];
	CHECK(int(offset[0]) == -24); // Map pixel = picture pixel + offset: the content sits 24 px to the left.
	CHECK(int(offset[1]) == 0);
	// Back in place: the same as importing the unshifted map.
	const Dictionary in_place = UZUMapImport::import_map(truth, pic, UZUMapImport::MAP_DEPTH, UZUMapImport::DEPTH_DISPARITY);
	offset = Dictionary(in_place["found"])["offset_px"];
	CHECK(int(offset[0]) == 0);
	CHECK(int(offset[1]) == 0);
	CHECK(difference(r["image"], in_place["image"]) < 1.0);
	// Without alignment the offset stays.
	const Dictionary kept = UZUMapImport::import_map(depth_map(24), pic, UZUMapImport::MAP_DEPTH, UZUMapImport::DEPTH_DISPARITY, false);
	CHECK(difference(kept["image"], in_place["image"]) > 5.0);
}

TEST_CASE("[UZUMapImport] Depth: four variants all come out as the same disparity") {
	// The truth: nearer toward the bottom (ground), with relief; white = near.
	auto disparity = [](int x, int y) { return 0.15f + 0.6f * float(y) / H + 0.2f * (height(x, y) - 0.5f); };
	struct Variant4 {
		bool white_far;
		bool narrow;
	};
	const Variant4 variants[] = { { false, false }, { true, false }, { false, true }, { true, true } };
	const Ref<Image> pic = picture();
	for (const Variant4 &v : variants) {
		Ref<Image> map = Image::create_empty(W, H, false, Image::FORMAT_L8);
		for (int y = 0; y < H; y++) {
			for (int x = 0; x < W; x++) {
				float d = disparity(x, y);
				if (v.white_far) {
					d = 1.0f - d;
				}
				if (v.narrow) {
					d = 0.4f + 0.2f * d;
				}
				map->set_pixel(x, y, Color(d, d, d));
			}
		}
		const Dictionary r = UZUMapImport::import_map(map, pic, UZUMapImport::MAP_DEPTH);
		REQUIRE(bool(r["ok"]));
		CHECK(String(Dictionary(r["found"])["white_is"]) == (v.white_far ? "far" : "near"));
		const Ref<Image> out = r["image"];
		// Correlation with the truth.
		double sa = 0, sb = 0, sab = 0, saa = 0, sbb = 0;
		int n = 0;
		for (int y = 0; y < H; y += 4) {
			for (int x = 0; x < W; x += 4) {
				const double a = out->get_pixel(x, y).r;
				const double b = disparity(x, y);
				sa += a;
				sb += b;
				sab += a * b;
				saa += a * a;
				sbb += b * b;
				n++;
			}
		}
		const double corr = (n * sab - sa * sb) / Math::sqrt((n * saa - sa * sa) * (n * sbb - sb * sb));
		CHECK(corr > 0.99);
		// Stretched to the full range.
		float lo = 1.0f, hi = 0.0f;
		for (int y = 0; y < H; y += 8) {
			for (int x = 0; x < W; x += 8) {
				lo = MIN(lo, out->get_pixel(x, y).r);
				hi = MAX(hi, out->get_pixel(x, y).r);
			}
		}
		CHECK(hi - lo > 0.9f);
	}
}

} // namespace TestUZUMapImport
