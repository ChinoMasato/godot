/**************************************************************************/
/*  test_uzu_picture.cpp                                                  */
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

TEST_FORCE_LINK(test_uzu_picture)

#ifndef _3D_DISABLED

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/uzu_picture.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/mesh.h"

namespace TestUZUPicture {

// A small picture: a dark blue night with a red tower in the middle that is near (its
// depth jumps), and a depth map that is nearer toward the bottom.
const int W = 320;
const int H = 200;

static bool in_tower(int x, int y) {
	return x >= 140 && x < 180 && y >= 40;
}

static String write_files() {
	const String dir = OS::get_singleton()->get_temp_path().path_join("uzu_picture_test");
	DirAccess::make_dir_recursive_absolute(dir);
	Ref<Image> art = Image::create_empty(W, H, false, Image::FORMAT_RGB8);
	Ref<Image> depth = Image::create_empty(W, H, false, Image::FORMAT_L8);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			art->set_pixel(x, y, in_tower(x, y) ? Color(0.8, 0.1, 0.1) : Color(0.1, 0.15, 0.4));
			const float d = in_tower(x, y) ? 0.8f : 0.1f + 0.3f * y / H;
			depth->set_pixel(x, y, Color(d, d, d));
		}
	}
	art->save_png(dir.path_join("art.png"));
	depth->save_png(dir.path_join("depth.png"));
	return dir;
}

static Dictionary make_spec(const String &p_dir) {
	const String text = R"({
		"image": "DIR/art.png",
		"depth": {"path": "DIR/depth.png", "kind": "disparity", "near": 4.5, "far": 160},
		"fov": 38,
		"lights": [{"at": [160, 60], "color": "#ffa64d", "energy": 2.0, "flicker": 1.0}, {"at": [40, 150]}],
		"moon": {"at": [260, 20]},
		"effects": {
			"rain": {"amount": 400},
			"fog": {"layers": [6, 20], "amount": 0.4},
			"lightning": {"first": 1.0, "every": [4, 8], "seed": 3},
			"embers": {"from": [[160, 70], [150, 90]], "amount": 20},
			"clouds": {"amount": 0.5}
		},
		"look": {"quality": "standard", "atmosphere": "night"}
	})";
	return JSON::parse_string(text.replace("DIR", p_dir));
}

TEST_CASE("[SceneTree][UZUPicture] Builds the scene from a spec") {
	const String dir = write_files();
	UZUPicture *picture = memnew(UZUPicture);
	picture->set_spec(make_spec(dir));
	const Dictionary stats = picture->build();
	REQUIRE(bool(stats["ok"]));
	CHECK(picture->is_built());

	const char *expected[] = { "Relief", "Backdrop", "Light0", "Light1", "Moon", "Rain", "Fog0", "Fog1", "Lightning", "Embers0", "Embers1", "Camera", "Look" };
	for (const char *name : expected) {
		CHECK_MESSAGE(picture->has_node(NodePath(name)), name);
	}
	CHECK(int(stats["lights"]) == 2);
	CHECK(int(stats["fog_sheets"]) == 2);
	CHECK(bool(stats["lightning"]));
	CHECK(int(stats["ember_sources"]) == 2);
	CHECK(int(stats["torn_triangles"]) > 0); // Torn around the tower.
	CHECK(float(stats["backdrop_filled_share"]) > 0.0); // The tower painted out of the backdrop.

	// The relief mesh: a grid over the picture minus the torn triangles.
	MeshInstance3D *relief = Object::cast_to<MeshInstance3D>(picture->get_node(NodePath("Relief")));
	Ref<ArrayMesh> mesh = relief->get_mesh();
	REQUIRE(mesh.is_valid());
	const Vector2i grid = stats["grid"];
	CHECK(grid.x == 360);
	const int triangles = mesh->surface_get_array_index_len(0) / 3;
	CHECK(triangles == grid.x * grid.y * 2 - int(stats["torn_triangles"]));

	// Lights sit just in front of the surface at their picture pixel.
	OmniLight3D *light = Object::cast_to<OmniLight3D>(picture->get_node(NodePath("Light0")));
	CHECK(light->get_position().is_equal_approx(picture->point_at(Vector2(160, 60)) * 0.93));
	Camera3D *camera = Object::cast_to<Camera3D>(picture->get_node(NodePath("Camera")));
	CHECK(camera->get_fov() < 38.0); // A little less than the picture (overscan).
	WorldEnvironment *look = Object::cast_to<WorldEnvironment>(picture->get_node(NodePath("Look")));
	CHECK(look->get_compositor().is_valid()); // The look's post effect.

	// Rebuilding replaces the built nodes instead of adding more.
	const int children = picture->get_child_count();
	picture->build();
	CHECK(picture->get_child_count() == children);
	memdelete(picture);
}

TEST_CASE("[SceneTree][UZUPicture] Picture pixels and depth to space") {
	const String dir = write_files();
	UZUPicture *picture = memnew(UZUPicture);
	picture->set_spec(make_spec(dir));
	REQUIRE(bool(picture->build()["ok"]));
	const float tan_half = Math::tan(Math::deg_to_rad(38.0f) * 0.5f);
	const float aspect = float(W) / H;
	// Disparity 1 is the near plane, 0 the far plane; the center pixel is on the axis.
	CHECK(picture->place(Vector2(W / 2, H / 2), 1.0).is_equal_approx(Vector3(0, 0, -4.5)));
	CHECK(picture->place(Vector2(W / 2, H / 2), 0.0).is_equal_approx(Vector3(0, 0, -160)));
	// The top-left corner at depth 1.
	CHECK(picture->place(Vector2(0, 0), 1.0).is_equal_approx(Vector3(-tan_half * aspect * 4.5f, tan_half * 4.5f, -4.5f)));
	// The depth map, read back (8-bit) and interpolated.
	CHECK(picture->depth_at(Vector2(160, 100)) == doctest::Approx(0.8).epsilon(0.01));
	CHECK(picture->depth_at(Vector2(20, 0)) == doctest::Approx(0.1).epsilon(0.02));
	CHECK(picture->point_at(Vector2(160, 100)).z > picture->point_at(Vector2(20, 100)).z); // The tower is nearer.
	memdelete(picture);
}

TEST_CASE("[SceneTree][UZUPicture] A spec that cannot be used") {
	UZUPicture *picture = memnew(UZUPicture);
	Dictionary spec;
	spec["image"] = "/nonexistent/art.png";
	picture->set_spec(spec);
	ERR_PRINT_OFF;
	const Dictionary stats = picture->build();
	ERR_PRINT_ON;
	CHECK_FALSE(bool(stats["ok"]));
	CHECK(String(stats["error"]).contains("image"));
	CHECK_FALSE(picture->is_built());
	CHECK(picture->get_child_count() == 0);
	memdelete(picture);
}

TEST_CASE("[UZUPicture] The building blocks: farthest depth nearby, and the fill behind") {
	// min_filter: the smallest value within the radius.
	Vector<float> src;
	src.resize(25);
	src.fill(0.9f);
	src.write[12] = 0.1f; // The center of a 5x5 grid.
	const Vector<float> out = UZUPicture::min_filter(src, 5, 5, 1);
	CHECK(out[12] == doctest::Approx(0.1));
	CHECK(out[6] == doctest::Approx(0.1)); // Diagonal neighbor.
	CHECK(out[0] == doctest::Approx(0.9)); // Two cells away.

	// fill_behind: the red tower, nearer than the backdrop depth, is painted out and filled
	// with the blue around it.
	const int sx = 32;
	const int sy = 20;
	Ref<Image> art = Image::create_empty(W, H, false, Image::FORMAT_RGB8);
	Vector<float> d, back;
	d.resize((sx + 1) * (sy + 1));
	back.resize(d.size());
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			art->set_pixel(x, y, in_tower(x, y) ? Color(0.8, 0.1, 0.1) : Color(0.1, 0.15, 0.4));
		}
	}
	for (int j = 0; j <= sy; j++) {
		for (int i = 0; i <= sx; i++) {
			const bool tower = in_tower(i * W / sx, j * H / sy);
			d.write[j * (sx + 1) + i] = tower ? 0.8f : 0.2f;
			back.write[j * (sx + 1) + i] = 0.2f;
		}
	}
	float share = 0.0;
	const Ref<Image> filled = UZUPicture::fill_behind(art, d, back, sx, sy, &share);
	CHECK(filled->get_width() == W / 2);
	CHECK(share > 0.05);
	const Color center = filled->get_pixel(80, 60); // Inside the tower (the result is half size).
	CHECK(center.b > center.r); // Blue now, not red.
	// Away from the tower the picture is kept (within 8-bit rounding).
	CHECK(Math::abs(filled->get_pixel(10, 10).b - 0.4f) < 0.02f);
}

} // namespace TestUZUPicture

#endif // _3D_DISABLED
