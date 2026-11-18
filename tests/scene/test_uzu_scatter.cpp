/**************************************************************************/
/*  test_uzu_scatter.cpp                                                  */
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

TEST_FORCE_LINK(test_uzu_scatter)

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/uzu_scatter.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/surface_tool.h"

namespace TestUZUScatter {

// A flat square of p_size meters at y = 0 (two triangles).
static Ref<ArrayMesh> ground(float p_size) {
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	const float h = p_size * 0.5f;
	const Vector3 a(-h, 0, -h), b(h, 0, -h), c(h, 0, h), d(-h, 0, h);
	for (const Vector3 &v : { a, b, c, a, c, d }) {
		st->add_vertex(v);
	}
	return st->commit();
}

TEST_CASE("[SceneTree][UZUScatter] Density, seed and placement on the surface") {
	const Ref<ArrayMesh> mesh = ground(10.0); // 100 square meters.
	Dictionary options;
	options["density"] = 2.0;
	options["seed"] = 7;
	options["scale_min"] = 0.5;
	options["scale_max"] = 1.5;
	Dictionary stats;
	const Vector<Transform3D> a = UZUScatter::compute(mesh, Transform3D(), options, &stats);
	CHECK(a.size() == 200);
	CHECK(double(stats["area"]) == doctest::Approx(100.0));
	CHECK(int(stats["target"]) == 200);
	for (const Transform3D &t : a) {
		CHECK(Math::abs(t.origin.y) < 1e-4); // On the ground.
		CHECK(Math::abs(t.origin.x) <= 5.0001);
		CHECK(Math::abs(t.origin.z) <= 5.0001);
		const float s = t.basis.get_scale().x;
		CHECK(s >= 0.5f - 1e-4f);
		CHECK(s <= 1.5f + 1e-4f);
		CHECK(t.basis.get_column(1).normalized().is_equal_approx(Vector3(0, 1, 0))); // Upright.
	}
	// Evenly spread: each quarter of the square gets about a quarter.
	int quarter = 0;
	for (const Transform3D &t : a) {
		quarter += t.origin.x < 0 && t.origin.z < 0;
	}
	CHECK(quarter > 30);
	CHECK(quarter < 70);
	// The same seed gives the same layout; another seed another one.
	const Vector<Transform3D> again = UZUScatter::compute(mesh, Transform3D(), options);
	REQUIRE(again.size() == a.size());
	for (int i = 0; i < a.size(); i++) {
		CHECK(again[i].is_equal_approx(a[i]));
	}
	options["seed"] = 8;
	CHECK_FALSE(UZUScatter::compute(mesh, Transform3D(), options)[0].is_equal_approx(a[0]));
	// The surface transform moves the props with it.
	const Vector<Transform3D> moved = UZUScatter::compute(mesh, Transform3D(Basis(), Vector3(100, 3, 0)), options);
	CHECK(Math::abs(moved[0].origin.y - 3.0) < 1e-4);
	CHECK(moved[0].origin.x > 90.0);
}

TEST_CASE("[SceneTree][UZUScatter] Exclusion, spacing, slope and limits") {
	const Ref<ArrayMesh> mesh = ground(10.0);
	Dictionary options;
	options["density"] = 3.0;
	Array zones;
	Dictionary circle;
	circle["center"] = Vector3(0, 0, 0);
	circle["radius"] = 2.0;
	zones.push_back(circle);
	Dictionary box;
	box["aabb"] = AABB(Vector3(3, -1, -5), Vector3(2, 2, 10));
	zones.push_back(box);
	options["exclude"] = zones;
	Dictionary stats;
	const Vector<Transform3D> kept = UZUScatter::compute(mesh, Transform3D(), options, &stats);
	CHECK(int(stats["rejected_excluded"]) > 0);
	for (const Transform3D &t : kept) {
		CHECK(Vector2(t.origin.x, t.origin.z).length() > 2.0f);
		CHECK(t.origin.x < 3.0f);
	}

	options.erase("exclude");
	options["min_distance"] = 0.8;
	const Vector<Transform3D> spaced = UZUScatter::compute(mesh, Transform3D(), options, &stats);
	CHECK(int(stats["rejected_spacing"]) > 0);
	for (int i = 0; i < spaced.size(); i++) {
		for (int j = i + 1; j < spaced.size(); j++) {
			CHECK(spaced[i].origin.distance_to(spaced[j].origin) >= 0.8f - 1e-4f);
		}
	}

	// Ground tilted by 40 degrees: none above a 30-degree limit, all below a 50-degree one;
	// with align 1 the props lean with the ground.
	const Transform3D tilted(Basis(Vector3(1, 0, 0), Math::deg_to_rad(40.0)), Vector3());
	options.erase("min_distance");
	options["max_slope"] = 30.0;
	CHECK(UZUScatter::compute(mesh, tilted, options, &stats).is_empty());
	CHECK(int(stats["rejected_slope"]) == int(stats["target"]));
	options["max_slope"] = 50.0;
	options["align"] = 1.0;
	const Vector<Transform3D> leaning = UZUScatter::compute(mesh, tilted, options);
	REQUIRE_FALSE(leaning.is_empty());
	CHECK(leaning[0].basis.get_column(1).normalized().angle_to(Vector3(0, 1, 0)) == doctest::Approx(Math::deg_to_rad(40.0)).epsilon(0.01));

	options["max_count"] = 10;
	CHECK(UZUScatter::compute(mesh, Transform3D(), options).size() == 10);
	options["density"] = 0.0;
	CHECK(UZUScatter::compute(mesh, Transform3D(), options).is_empty());

	ERR_PRINT_OFF;
	Dictionary wrong;
	wrong["densty"] = 1.0;
	CHECK(UZUScatter::compute(mesh, Transform3D(), wrong).is_empty());
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][UZUScatter] One MultiMesh in the scene") {
	Node3D *root = memnew(Node3D);
	Node3D *holder = memnew(Node3D);
	holder->set_position(Vector3(0, 2, 0));
	root->add_child(holder);
	MeshInstance3D *floor = memnew(MeshInstance3D);
	floor->set_mesh(ground(4.0));
	floor->set_position(Vector3(10, 0, 0));
	holder->add_child(floor);
	Ref<BoxMesh> pebble;
	pebble.instantiate();
	Dictionary options;
	options["density"] = 5.0;
	const Dictionary r = UZUScatter::scatter(root, floor, pebble, options, "Pebbles");
	REQUIRE(bool(r["ok"]));
	CHECK(int(r["count"]) == 80);
	CHECK(String(r["node"]) == "Pebbles");
	MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(root->get_node(NodePath("Pebbles")));
	REQUIRE(mmi != nullptr);
	CHECK(mmi->get_owner() == root);
	Ref<MultiMesh> mm = mmi->get_multimesh();
	REQUIRE(mm.is_valid());
	CHECK(mm->get_instance_count() == 80);
	CHECK(mm->get_mesh() == pebble);
	// Placed where the floor is, relative to the root (through the holder).
	const AABB bounds = r["bounds"];
	CHECK(Math::abs(bounds.position.y - 2.0) < 1e-4);
	CHECK(bounds.size.y < 1e-4);
	CHECK(bounds.position.x > 7.99);
	CHECK(bounds.get_end().x < 12.01);
	CHECK(bounds.size.x > 3.0); // Spread over the 4 m floor.
	// The saved data (the buffer: basis rows with the origin) holds the same positions.
	const Vector<float> buffer = mm->get_buffer();
	REQUIRE(buffer.size() == 80 * 12);
	for (int i = 0; i < 80; i++) {
		const Vector3 p(buffer[i * 12 + 3], buffer[i * 12 + 7], buffer[i * 12 + 11]);
		CHECK(bounds.grow(1e-4).has_point(p));
		CHECK(Math::abs(p.y - 2.0) < 1e-4);
	}

	const Dictionary bad = UZUScatter::scatter(root, nullptr, pebble);
	CHECK_FALSE(bool(bad["ok"]));
	memdelete(root);
}

} // namespace TestUZUScatter
