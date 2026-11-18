/**************************************************************************/
/*  test_uzu_pbr_from_image.cpp                                           */
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

TEST_FORCE_LINK(test_uzu_pbr_from_image)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "scene/resources/material.h"
#include "scene/resources/uzu_pbr_from_image.h"

namespace TestUZUPBRFromImage {

// A known surface: rounded stones between dark mortar lines. The texture is the height
// as brightness, times a strong light gradient from left to right (baked shading).
const int W = 256;
const int H = 192;

static float height(int x, int y) {
	const float cell = 32.0f;
	const float lx = Math::fposmod(float(x), cell);
	const float ly = Math::fposmod(float(y), cell);
	const float d = MIN(MIN(lx, cell - lx), MIN(ly, cell - ly)); // Distance to the mortar.
	const float t = CLAMP(d / 8.0f, 0.0f, 1.0f);
	return 0.15f + 0.7f * t * t * (3.0f - 2.0f * t);
}

static float light(int x) {
	return 0.3f + 0.7f * float(x) / (W - 1);
}

static Ref<Image> texture() {
	Ref<Image> img = Image::create_empty(W, H, false, Image::FORMAT_RGB8);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			const float v = height(x, y) * light(x);
			img->set_pixel(x, y, Color(v, v * 0.95f, v * 0.9f));
		}
	}
	return img;
}

static double correlation(const Vector<float> &p_a, const Vector<float> &p_b) {
	double ma = 0.0, mb = 0.0;
	for (int i = 0; i < p_a.size(); i++) {
		ma += p_a[i];
		mb += p_b[i];
	}
	ma /= p_a.size();
	mb /= p_b.size();
	double cov = 0.0, va = 0.0, vb = 0.0;
	for (int i = 0; i < p_a.size(); i++) {
		cov += (p_a[i] - ma) * (p_b[i] - mb);
		va += (p_a[i] - ma) * (p_a[i] - ma);
		vb += (p_b[i] - mb) * (p_b[i] - mb);
	}
	return cov / Math::sqrt(MAX(va * vb, 1e-12));
}

// Mean luminance of a column band.
static float band(const Ref<Image> &p_image, int p_x0, int p_x1) {
	double sum = 0.0;
	for (int y = 0; y < p_image->get_height(); y++) {
		for (int x = p_x0; x < p_x1; x++) {
			sum += p_image->get_pixel(x, y).get_luminance();
		}
	}
	return sum / (p_image->get_height() * (p_x1 - p_x0));
}

TEST_CASE("[UZUPBRFromImage] Kinds") {
	const Dictionary kinds = UZUPBRFromImage::get_kinds();
	CHECK(kinds.size() == 10);
	CHECK(float(Dictionary(kinds["stone"])["strength"]) == doctest::Approx(0.55));
	CHECK(float(Dictionary(kinds["metal"])["metal"]) == doctest::Approx(1.0));
	CHECK(float(Dictionary(kinds["brick"])["metal"]) == doctest::Approx(0.0));
}

TEST_CASE("[UZUPBRFromImage] Maps from one texture") {
	const Ref<Image> src = texture();
	const Dictionary r = UZUPBRFromImage::make(src, "stone");
	REQUIRE(bool(r["ok"]));
	const Ref<Image> albedo = r["albedo"];
	const Ref<Image> normal = r["normal"];
	const Ref<Image> orm = r["orm"];
	const Ref<Image> height_map = r["height"];
	REQUIRE(albedo.is_valid());
	CHECK(albedo->get_size() == Size2i(W, H));
	CHECK(normal->get_format() == Image::FORMAT_RGB8);
	CHECK(height_map->get_format() == Image::FORMAT_L8);

	SUBCASE("Albedo: the baked light gradient is mostly taken out") {
		const float before = band(src, W - 48, W) / band(src, 0, 48);
		const float after = band(albedo, W - 48, W) / band(albedo, 0, 48);
		CHECK(before > 1.8f);
		CHECK(after < 1.0f + (before - 1.0f) * 0.6f);
		// And the warning says so.
		const Array warnings = r["warnings"];
		REQUIRE(warnings.size() >= 1);
		CHECK(String(warnings[0]).contains("uneven lighting"));
	}

	// The true height and its derivatives, away from the image border.
	Vector<float> truth, got, gx, gy, nr, ng;
	for (int y = 2; y < H - 2; y++) {
		for (int x = 2; x < W - 2; x++) {
			truth.push_back(height(x, y));
			got.push_back(height_map->get_pixel(x, y).r);
			gx.push_back(-(height(x + 1, y) - height(x - 1, y)));
			gy.push_back(height(x, y + 1) - height(x, y - 1));
			const Color n = normal->get_pixel(x, y);
			nr.push_back(n.r);
			ng.push_back(n.g);
		}
	}

	SUBCASE("Height follows the stones, normals follow the height (OpenGL)") {
		CHECK(correlation(got, truth) > 0.8);
		// Red leans toward -dh/dx; green toward +dh/dy (rows go down: green is up).
		CHECK(correlation(nr, gx) > 0.6);
		CHECK(correlation(ng, gy) > 0.6);
	}

	SUBCASE("ORM: darker AO and rougher in the mortar, no metal") {
		const Color top = orm->get_pixel(16 + 32 * 3, 16 + 32 * 2); // The middle of a stone.
		const Color mortar = orm->get_pixel(32 * 3, 16 + 32 * 2);
		CHECK(mortar.r < top.r);
		CHECK(mortar.g > top.g);
		CHECK(top.b == doctest::Approx(0.0));
		const Dictionary stats = r["stats"];
		CHECK(float(stats["roughness_mean"]) == doctest::Approx(0.82).epsilon(0.08));
		CHECK(float(stats["normal_strength"]) == doctest::Approx(0.55));
	}

	SUBCASE("The material uses the maps") {
		const Ref<ORMMaterial3D> m = r["material"];
		REQUIRE(m.is_valid());
		CHECK(m->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING));
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid());
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_NORMAL).is_valid());
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_ORM).is_valid());
		CHECK(String(r["normal_source"]) == "brightness");
	}
}

TEST_CASE("[UZUPBRFromImage] Options") {
	const Ref<Image> src = texture();
	SUBCASE("Metal is metallic") {
		const Dictionary r = UZUPBRFromImage::make(src, "metal");
		CHECK(Ref<Image>(r["orm"])->get_pixel(10, 10).b == doctest::Approx(1.0));
	}
	SUBCASE("Invert: dark is high") {
		const Ref<Image> a = UZUPBRFromImage::make(src, "stone")["height"];
		const Dictionary options = { { "invert", true } };
		const Ref<Image> b = UZUPBRFromImage::make(src, "stone", options)["height"];
		Vector<float> va, vb;
		for (int i = 0; i < W * H; i += 7) {
			va.push_back(a->get_pixel(i % W, i / W).r);
			vb.push_back(b->get_pixel(i % W, i / W).r);
		}
		CHECK(correlation(va, vb) < -0.8);
	}
	SUBCASE("Size: wide images are scaled down") {
		const Dictionary options = { { "size", 128 } };
		const Dictionary r = UZUPBRFromImage::make(src, "stone", options);
		CHECK(Ref<Image>(r["albedo"])->get_size() == Size2i(128, 96));
	}
	SUBCASE("Tile: the normal at the left edge sees the right edge") {
		// A seam on purpose: bright right column. Tiled, the left column's normal leans
		// away from it; not tiled, the left column sees only itself.
		Ref<Image> seam = src->duplicate();
		for (int y = 0; y < H; y++) {
			seam->set_pixel(W - 1, y, Color(1, 1, 1));
		}
		const Dictionary tiled = { { "tile", true }, { "delight", 0.0 } };
		const Dictionary flat = { { "delight", 0.0 } };
		const float with = Ref<Image>(UZUPBRFromImage::make(seam, "stone", tiled)["normal"])->get_pixel(0, 16).r;
		const float without = Ref<Image>(UZUPBRFromImage::make(seam, "stone", flat)["normal"])->get_pixel(0, 16).r;
		CHECK(with > without + 0.02f);
	}
	SUBCASE("A given normal map replaces the estimate") {
		Ref<Image> given = Image::create_empty(W / 2, H / 2, false, Image::FORMAT_RGBA8);
		given->fill(Color(0.5, 0.5, 1.0));
		const Dictionary options = { { "normal", given } };
		const Dictionary r = UZUPBRFromImage::make(src, "stone", options);
		const Ref<Image> n = r["normal"];
		CHECK(n->get_size() == Size2i(W, H));
		CHECK(n->get_format() == Image::FORMAT_RGB8);
		const Color c = n->get_pixel(5, 5);
		CHECK(Math::abs(c.r - 0.5f) < 0.01f);
		CHECK(Math::abs(c.g - 0.5f) < 0.01f);
		CHECK(c.b > 0.99f);
		CHECK(String(r["normal_source"]) == "image");
	}
	SUBCASE("Errors") {
		CHECK_FALSE(bool(UZUPBRFromImage::make(src, "nope")["ok"]));
		CHECK_FALSE(bool(UZUPBRFromImage::make(Ref<Image>(), "stone")["ok"]));
	}
}

TEST_CASE("[UZUPBRFromImage] Compare with a reference") {
	const Dictionary r = UZUPBRFromImage::make(texture(), "stone");
	const Ref<Image> normal = r["normal"];
	const Ref<Image> orm = r["orm"];
	const Dictionary same = UZUPBRFromImage::compare(normal, normal, orm, orm);
	CHECK(float(same["normal_angle_error_deg"]) < 0.5f);
	CHECK(float(same["flat_normal_error_deg"]) > 1.0f);
	CHECK(float(same["roughness_correlation"]) == doctest::Approx(1.0));
	Ref<Image> flat = Image::create_empty(64, 64, false, Image::FORMAT_RGB8);
	flat->fill(Color(0.5, 0.5, 1.0));
	const Dictionary against_flat = UZUPBRFromImage::compare(flat, normal);
	CHECK(float(against_flat["normal_angle_error_deg"]) == doctest::Approx(float(against_flat["flat_normal_error_deg"])).epsilon(0.05));
	CHECK_FALSE(against_flat.has("roughness_correlation"));
	CHECK_FALSE(bool(UZUPBRFromImage::compare(Ref<Image>(), normal)["ok"]));
}

TEST_CASE("[UZUPBRFromImage] Files") {
	const String dir = OS::get_singleton()->get_temp_path().path_join("uzu_pbr_test");
	const String in = dir.path_join("stone.png");
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(texture()->save_png(in) == OK);
	const Dictionary r = UZUPBRFromImage::make_file(in, dir.path_join("out"), "brick");
	REQUIRE(bool(r["ok"]));
	CHECK_FALSE(r.has("albedo"));
	const Dictionary files = r["files"];
	for (const char *name : { "albedo", "normal", "orm", "height" }) {
		CHECK_MESSAGE(FileAccess::exists(files[name]), name);
	}
	const Ref<ORMMaterial3D> m = ResourceLoader::load(files["material"]);
	REQUIRE(m.is_valid());
	CHECK(m->get_texture(BaseMaterial3D::TEXTURE_NORMAL).is_valid());
	ERR_PRINT_OFF;
	CHECK_FALSE(bool(UZUPBRFromImage::make_file(dir.path_join("missing.png"), dir, "stone")["ok"]));
	const Dictionary bad_normal = { { "normal_from", dir.path_join("missing.png") } };
	CHECK(String(UZUPBRFromImage::make_file(in, dir, "stone", bad_normal)["error"]).contains("normal"));
	ERR_PRINT_ON;
}

} // namespace TestUZUPBRFromImage
