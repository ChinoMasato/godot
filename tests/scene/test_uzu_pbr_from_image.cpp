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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_uzu_pbr_from_image)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/resources/uzu_pbr_from_image.h"

namespace TestUZUPBRFromImage {

static Ref<Image> flat_image(int p_w, int p_h, const Color &p_color) {
	Ref<Image> image = Image::create_empty(p_w, p_h, false, Image::FORMAT_RGB8);
	for (int y = 0; y < p_h; y++) {
		for (int x = 0; x < p_w; x++) {
			image->set_pixel(x, y, p_color);
		}
	}
	return image;
}

TEST_CASE("[UZUPBRFromImage] Registered, with the fitted kind table") {
	CHECK(ClassDB::class_exists("UZUPBRFromImage"));
	CHECK(ClassDB::has_method("UZUPBRFromImage", "make"));
	CHECK(ClassDB::has_method("UZUPBRFromImage", "make_file"));
	CHECK(ClassDB::has_method("UZUPBRFromImage", "compare"));
	const Dictionary kinds = UZUPBRFromImage::kinds();
	CHECK(kinds.has("stone"));
	CHECK(kinds.has("cobble"));
	CHECK(kinds.has("painted_metal"));
	const Dictionary metal = kinds["metal"];
	CHECK(float(metal["metal"]) == doctest::Approx(1.0));
	const Dictionary stone = kinds["stone"];
	CHECK(float(stone["rough"]) == doctest::Approx(0.82));
	// An unknown kind is an error result, not a crash.
	const Dictionary bad = UZUPBRFromImage::make(flat_image(8, 8, Color(0.5, 0.5, 0.5)), "unobtanium");
	CHECK(!bool(bad["ok"]));
}

TEST_CASE("[UZUPBRFromImage] A flat photo gives a flat normal, full AO and the kind roughness") {
	const Dictionary result = UZUPBRFromImage::make(flat_image(64, 64, Color(0.5, 0.5, 0.5)), "stone");
	REQUIRE(bool(result["ok"]));
	CHECK(String(result["normal_source"]) == "brightness");
	const Array warnings = result["warnings"];
	CHECK(warnings.is_empty());
	Ref<Image> normal = result["normal"];
	REQUIRE(normal.is_valid());
	const Color n = normal->get_pixel(32, 32);
	CHECK(n.r > 0.48f);
	CHECK(n.r < 0.52f);
	CHECK(n.g > 0.48f);
	CHECK(n.g < 0.52f);
	CHECK(n.b > 0.98f);
	Ref<Image> orm = result["orm"];
	REQUIRE(orm.is_valid());
	const Color o = orm->get_pixel(32, 32);
	CHECK(o.r > 0.98f); // No cavities: AO stays 1.
	CHECK(o.b == doctest::Approx(0.0)); // Stone is not metallic.
	const Dictionary stats = result["stats"];
	// The mean roughness stays near the kind's base (0.82 for stone): the
	// cavity/height terms only move it by a fraction of the spread.
	CHECK(float(stats["roughness_mean"]) > 0.72f);
	CHECK(float(stats["roughness_mean"]) < 0.92f);
	CHECK(float(stats["seam"]) == doctest::Approx(0.0));
}

TEST_CASE("[UZUPBRFromImage] A brightness step tilts the normal at the step") {
	Ref<Image> image = Image::create_empty(64, 64, false, Image::FORMAT_RGB8);
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 64; x++) {
			const float v = x < 32 ? 0.25f : 0.75f;
			image->set_pixel(x, y, Color(v, v, v));
		}
	}
	// Cobble with a strength multiplier so the tilt is well above byte noise.
	const Dictionary result = UZUPBRFromImage::make(image, "cobble", 3.0f);
	REQUIRE(bool(result["ok"]));
	Ref<Image> normal = result["normal"];
	const Color at_step = normal->get_pixel(32, 32);
	const Color away = normal->get_pixel(4, 32);
	CHECK(away.r > 0.47f);
	CHECK(away.r < 0.53f);
	CHECK(at_step.r < 0.46f); // Bright is high, so the surface leans back (-x).
	CHECK(at_step.r < away.r - 0.03f);
}

TEST_CASE("[UZUPBRFromImage] The metal kind writes metallic 1 into the ORM blue channel") {
	const Dictionary result = UZUPBRFromImage::make(flat_image(32, 32, Color(0.4, 0.4, 0.45)), "metal");
	REQUIRE(bool(result["ok"]));
	Ref<Image> orm = result["orm"];
	CHECK(orm->get_pixel(16, 16).b > 0.98f);
}

TEST_CASE("[UZUPBRFromImage] Delight removes a lighting ramp from the albedo") {
	Ref<Image> image = Image::create_empty(256, 64, false, Image::FORMAT_RGB8);
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 256; x++) {
			const float v = float(x) / 255.0f;
			image->set_pixel(x, y, Color(v, v, v));
		}
	}
	auto window_mean = [](const Ref<Image> &p_img, int p_x0, int p_x1) {
		double sum = 0.0;
		int n = 0;
		for (int y = 0; y < p_img->get_height(); y++) {
			for (int x = p_x0; x < p_x1; x++) {
				sum += p_img->get_pixel(x, y).r;
				n++;
			}
		}
		return float(sum / n);
	};
	const Dictionary lit_result = UZUPBRFromImage::make(image, "stone", 1.0f, 0.0f);
	REQUIRE(bool(lit_result["ok"]));
	Ref<Image> lit_albedo = lit_result["albedo"];
	const float lit_diff = window_mean(lit_albedo, 216, 232) - window_mean(lit_albedo, 24, 40);
	CHECK(lit_diff > 0.2f);
	const Dictionary flat_result = UZUPBRFromImage::make(image, "stone", 1.0f, 1.0f);
	REQUIRE(bool(flat_result["ok"]));
	Ref<Image> flat_albedo = flat_result["albedo"];
	const float flat_diff = Math::abs(window_mean(flat_albedo, 216, 232) - window_mean(flat_albedo, 24, 40));
	// The slow-light estimate is coarse (a 64x shrink), so delight flattens
	// the ramp substantially rather than perfectly.
	CHECK(flat_diff < lit_diff * 0.5f);
}

TEST_CASE("[UZUPBRFromImage] Compare measures known angles") {
	Ref<Image> flat = flat_image(64, 64, Color(0.5, 0.5, 1.0));
	Ref<Image> tilt = flat_image(64, 64, Color(0.5, 0.8536, 0.8536)); // 45 degrees in +y.
	const Dictionary same = UZUPBRFromImage::compare(tilt, tilt);
	REQUIRE(bool(same["ok"]));
	CHECK(float(same["normal_angle_error_deg"]) < 0.6f);
	const Dictionary angled = UZUPBRFromImage::compare(flat, tilt);
	CHECK(float(angled["normal_angle_error_deg"]) > 43.0f);
	CHECK(float(angled["normal_angle_error_deg"]) < 47.0f);
	CHECK(float(angled["flat_normal_error_deg"]) > 43.0f);
	CHECK(float(angled["flat_normal_error_deg"]) < 47.0f);
	// Roughness correlation of a map with itself is 1.
	Ref<Image> orm = Image::create_empty(64, 64, false, Image::FORMAT_RGB8);
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 64; x++) {
			orm->set_pixel(x, y, Color(1.0, float(x) / 63.0f, 0.0));
		}
	}
	const Dictionary rough = UZUPBRFromImage::compare(flat, flat, orm, orm);
	CHECK(float(rough["roughness_correlation"]) > 0.99f);
	CHECK(float(rough["roughness_mean"]) == doctest::Approx(float(rough["ref_roughness_mean"])));
}

TEST_CASE("[UZUPBRFromImage] make_file writes the maps and the material") {
	const String dir = OS::get_singleton()->get_cache_path().path_join("uzu_pbr_test");
	DirAccess::make_dir_recursive_absolute(dir);
	const String in_path = dir.path_join("tex.png");
	REQUIRE(flat_image(32, 32, Color(0.6, 0.55, 0.5))->save_png(in_path) == OK);
	const String out_dir = dir.path_join("out");
	const Dictionary result = UZUPBRFromImage::make_file(in_path, out_dir, "wood");
	REQUIRE(bool(result["ok"]));
	const Dictionary files = result["files"];
	for (const char *name : { "albedo", "normal", "orm", "height", "material" }) {
		CHECK(FileAccess::exists(String(files[name])));
		DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(String(files[name])));
	}
	DirAccess::remove_absolute(in_path);
}

} // namespace TestUZUPBRFromImage
