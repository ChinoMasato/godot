/**************************************************************************/
/*  test_uzu_world_gen.h                                                  */
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

#pragma once

#include "../uzu_world_gen.h"

#include "core/math/geometry_2d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/navigation_mesh.h"
#include "tests/test_macros.h"

namespace TestUZUWorldGen {

static Dictionary small_nature(int p_seed) {
	Dictionary s;
	s["size"] = 64.0;
	s["cell"] = 2.0;
	s["seed"] = p_seed;
	s["trees"] = 3.0;
	s["rocks"] = 1.0;
	s["grass"] = 4.0;
	return s;
}

static PackedVector3Array mesh_vertices(Node *p_holder, const String &p_name) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_holder->get_node_or_null(NodePath(p_name)));
	if (!mi || mi->get_mesh().is_null()) {
		return PackedVector3Array();
	}
	return Array(mi->get_mesh()->surface_get_arrays(0))[Mesh::ARRAY_VERTEX];
}

TEST_CASE("[SceneTree][UZUWorldGen] A wrong spec says what to change and builds nothing") {
	Dictionary s;
	s["tree"] = 1;
	Dictionary r = UZUWorldGen::resolve("nature", s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["key"]) == "tree");
	CHECK(String(r["hint"]).contains("did you mean \"trees\""));

	s.clear();
	s["size"] = 5000;
	r = UZUWorldGen::resolve("nature", s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["key"]) == "size");
	CHECK(String(r["error"]).contains("2048"));

	s.clear();
	s["water"] = "yes";
	r = UZUWorldGen::resolve("nature", s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["error"]).contains("true or false"));

	s.clear();
	s["size"] = 2048;
	s["cell"] = 1;
	r = UZUWorldGen::resolve("nature", s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["key"]) == "cell");

	s.clear();
	s["floors_min"] = 6;
	s["floors_max"] = 2;
	r = UZUWorldGen::resolve("city", s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["key"]) == "floors_min");

	CHECK_FALSE(bool(UZUWorldGen::resolve("forest", Dictionary())["ok"]));

	Node3D *root = memnew(Node3D);
	s.clear();
	s["palette"] = "neon";
	r = UZUWorldGen::generate_city(root, s);
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["error"]).contains("warm"));
	CHECK(root->get_child_count() == 0);
	memdelete(root);
}

TEST_CASE("[SceneTree][UZUWorldGen] Presets fill the spec and the spec wins") {
	Dictionary s;
	s["preset"] = "mountains";
	Dictionary o = UZUWorldGen::resolve("nature", s)["options"];
	CHECK(double(o["height"]) == doctest::Approx(140.0));
	s["height"] = 50;
	o = UZUWorldGen::resolve("nature", s)["options"];
	CHECK(double(o["height"]) == doctest::Approx(50.0));
	CHECK(String(o["preset"]) == "mountains");

	const Dictionary options = UZUWorldGen::get_options("city");
	CHECK(options.has("floors_max"));
	CHECK(String(Dictionary(options["palette"])["accepts"]).contains("modern"));
	CHECK(UZUWorldGen::get_presets("nature").has("island"));
}

TEST_CASE("[SceneTree][UZUWorldGen] Nature: the same seed gives the same map") {
	Node3D *root_a = memnew(Node3D);
	Node3D *root_b = memnew(Node3D);
	Node3D *root_c = memnew(Node3D);
	const Dictionary a = UZUWorldGen::generate_nature(root_a, small_nature(3));
	const Dictionary b = UZUWorldGen::generate_nature(root_b, small_nature(3));
	const Dictionary c = UZUWorldGen::generate_nature(root_c, small_nature(4));
	REQUIRE(bool(a["ok"]));
	REQUIRE(bool(b["ok"]));
	REQUIRE(bool(c["ok"]));
	Node3D *nature = Object::cast_to<Node3D>(root_a->get_node_or_null(NodePath(String(a["node"]))));
	REQUIRE(nature);
	const PackedVector3Array va = mesh_vertices(nature, "Terrain");
	CHECK(va.size() == 33 * 33);
	CHECK(va == mesh_vertices(root_b->get_child(0), "Terrain"));
	CHECK(va != mesh_vertices(root_c->get_child(0), "Terrain"));
	CHECK(Dictionary(a["counts"]) == Dictionary(b["counts"]));
	CHECK(int(Dictionary(a["counts"])["trees"]) > 0);
	CHECK(int(Dictionary(a["counts"])["rocks"]) > 0);
	const Ref<Image> overview = a["overview"];
	REQUIRE(overview.is_valid());
	CHECK(overview->get_width() == 256);

	// height_at lies exactly on the terrain's triangles.
	for (int k = 0; k < va.size(); k += 97) {
		CHECK(UZUWorldGen::height_at(nature, va[k].x, va[k].z) == doctest::Approx(va[k].y).epsilon(0.0001));
	}
	CHECK(Math::is_nan(UZUWorldGen::height_at(nature, 1000, 0)));

	// The spawn is on dry ground, and every new node is saved with the scene.
	const Array spawn = Dictionary(Dictionary(a["places"])["spawn"])["position"];
	if (a["water_y"].get_type() != Variant::NIL) {
		CHECK(double(spawn[1]) > double(a["water_y"]));
	}
	CHECK(double(spawn[1]) == doctest::Approx(UZUWorldGen::height_at(nature, spawn[0], spawn[2])).epsilon(0.01));
	TypedArray<Node> all = nature->find_children("*", "", true, false);
	for (int i = 0; i < all.size(); i++) {
		CHECK(Object::cast_to<Node>(all[i])->get_owner() == root_a);
	}
	memdelete(root_a);
	memdelete(root_b);
	memdelete(root_c);
}

TEST_CASE("[SceneTree][UZUWorldGen] Island: sea at the edges, land in the middle") {
	Node3D *root = memnew(Node3D);
	Dictionary s = small_nature(5);
	s["preset"] = "island";
	s["size"] = 128;
	const Dictionary r = UZUWorldGen::generate_nature(root, s);
	REQUIRE(bool(r["ok"]));
	Node3D *nature = Object::cast_to<Node3D>(root->get_child(0));
	const float wy = double(r["water_y"]);
	CHECK(UZUWorldGen::height_at(nature, -63, -63) < wy);
	CHECK(UZUWorldGen::height_at(nature, 63, 0) < wy);
	CHECK(double(r["water_fraction"]) > 0.2);
	CHECK(double(r["water_fraction"]) < 0.9);
	CHECK(root->find_child("Water", true, false) != nullptr);
	memdelete(root);
}

TEST_CASE("[SceneTree][UZUWorldGen] City: buildings stay off the streets, the same seed gives the same city") {
	Dictionary s;
	s["size"] = 120;
	s["block"] = 40;
	s["road_width"] = 8;
	s["sidewalk"] = 2;
	s["seed"] = 9;
	s["max_lights"] = 5;
	Node3D *root = memnew(Node3D);
	Node3D *root2 = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_city(root, s);
	const Dictionary r2 = UZUWorldGen::generate_city(root2, s);
	REQUIRE(bool(r["ok"]));
	CHECK(Dictionary(r["counts"]) == Dictionary(r2["counts"]));
	CHECK(int(r["triangles"]) == int(r2["triangles"]));
	const Dictionary counts = r["counts"];
	CHECK(int(counts["buildings"]) > 0);
	CHECK(int(counts["lights"]) == 5);
	CHECK(int(counts["lamps"]) > 5);
	CHECK(Array(r["hints"]).size() > 0);
	CHECK(Dictionary(r["places"]).has("plaza"));
	CHECK(Dictionary(r["places"]).has("gate_east"));

	// Grid layout: streets on x, z = -60, -20, 20, 60. No wall corner on a street.
	Node *city = root->get_child(0);
	const PackedVector3Array v = mesh_vertices(city, "Buildings");
	REQUIRE(v.size() > 0);
	bool clear = true;
	for (int i = 0; i < v.size(); i++) {
		for (float line = -60; line <= 60; line += 40) {
			clear = clear && Math::abs(v[i].x - line) >= 4 && Math::abs(v[i].z - line) >= 4;
		}
		clear = clear && v[i].y >= 0.149f;
	}
	CHECK(clear);
	TypedArray<Node> all = city->find_children("*", "", true, false);
	for (int i = 0; i < all.size(); i++) {
		CHECK(Object::cast_to<Node>(all[i])->get_owner() == root);
	}
	memdelete(root);
	memdelete(root2);
}

TEST_CASE("[SceneTree][UZUWorldGen] Villages joined by roads: every road connected, gentle, clear of trees") {
	Dictionary spec;
	spec["preset"] = "hills";
	spec["size"] = 256.0;
	spec["cell"] = 2.0;
	spec["seed"] = 5;
	spec["trees"] = 4.0;
	spec["villages"] = 4;
	spec["road_width"] = 3.0;
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_nature(root, spec);
	REQUIRE(bool(r["ok"]));
	const Array villages = Dictionary(r["places"])["villages"];
	REQUIRE(villages.size() >= 2);
	const Array roads = r["roads"];
	// A spanning tree: one road fewer than villages, and every village reachable by road.
	CHECK(roads.size() == villages.size() - 1);
	CHECK(bool(r["roads_connected"]));
	Node3D *nature = Object::cast_to<Node3D>(root->get_node_or_null(NodePath(String(r["node"]))));
	REQUIRE(nature);
	CHECK(nature->get_node_or_null(NodePath("Roads")) != nullptr);
	const double cell = 2.0;
	for (int i = 0; i < roads.size(); i++) {
		const Dictionary road = roads[i];
		REQUIRE(bool(road["connected"]));
		const Array pts = road["points"];
		REQUIRE(pts.size() >= 2);
		// The road starts at one village and ends at the other.
		auto village_at = [&](const String &p_name) {
			for (int v = 0; v < villages.size(); v++) {
				if (String(Dictionary(villages[v])["name"]) == p_name) {
					const Array p = Dictionary(villages[v])["position"];
					return Vector3(p[0], p[1], p[2]);
				}
			}
			return Vector3(1e9, 0, 0);
		};
		const Array first = pts[0];
		const Array last = pts[pts.size() - 1];
		CHECK(Vector3(first[0], first[1], first[2]).is_equal_approx(village_at(road["from"])));
		CHECK(Vector3(last[0], last[1], last[2]).is_equal_approx(village_at(road["to"])));
		// Every section is continuous: neighbouring grid points, on the ground, above water.
		for (int k = 1; k < pts.size(); k++) {
			const Array a = pts[k - 1];
			const Array b = pts[k];
			const double step = Vector2(double(b[0]) - double(a[0]), double(b[2]) - double(a[2])).length();
			CHECK(step <= cell * 1.4143);
			CHECK(step > 0.0);
			CHECK(double(b[1]) == doctest::Approx(UZUWorldGen::height_at(nature, b[0], b[2])).epsilon(0.001));
			if (r["water_y"].get_type() != Variant::NIL) {
				CHECK(double(b[1]) > double(r["water_y"]));
			}
		}
		CHECK(double(road["max_grade"]) <= 0.35);
		CHECK(double(road["length"]) > 0.0);
	}
	// No tree stands on a road.
	MultiMeshInstance3D *trees = Object::cast_to<MultiMeshInstance3D>(nature->get_node_or_null(NodePath("TreesBroadleaf")));
	if (trees && trees->get_multimesh().is_valid()) {
		const Vector<float> buf = trees->get_multimesh()->get_buffer();
		const Array pts = Dictionary(roads[0])["points"];
		for (int t = 0; t < buf.size() / 12; t++) {
			const Vector2 tree(buf[t * 12 + 3], buf[t * 12 + 11]);
			for (int k = 0; k < pts.size(); k++) {
				const Array p = pts[k];
				CHECK(tree.distance_to(Vector2(p[0], p[2])) > 1.5);
			}
		}
	}
	// The same spec gives the same roads; no villages, no roads.
	Node3D *again = memnew(Node3D);
	CHECK(Array(UZUWorldGen::generate_nature(again, spec)["roads"]) == roads);
	spec["villages"] = 0;
	Node3D *none = memnew(Node3D);
	const Dictionary n = UZUWorldGen::generate_nature(none, spec);
	CHECK_FALSE(n.has("roads"));
	CHECK(none->get_child(0)->get_node_or_null(NodePath("Roads")) == nullptr);
	memdelete(root);
	memdelete(again);
	memdelete(none);
}

TEST_CASE("[SceneTree][UZUWorldGen] City streets: benches placed, a navigation mesh that joins every place and keeps out of buildings") {
	Dictionary s;
	s["size"] = 120;
	s["block"] = 40;
	s["road_width"] = 8;
	s["sidewalk"] = 2;
	s["seed"] = 4;
	s["benches"] = 20.0;
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_city(root, s);
	REQUIRE(bool(r["ok"]));
	const Dictionary counts = r["counts"];
	CHECK(int(counts["benches"]) > 4); // Along the sidewalks, plus four around the fountain.
	Node *city = root->get_node(NodePath(String(r["node"])));
	CHECK(city->get_node_or_null(NodePath("Benches")) != nullptr);
	// The places travel with the saved scene, for walking checks on it.
	REQUIRE(city->has_meta("uzu_places"));
	CHECK(Dictionary(city->get_meta("uzu_places")).has("gate_west"));

	REQUIRE(r.has("navigation"));
	const Dictionary nav = r["navigation"];
	CHECK(int(nav["polygons"]) > 0);
	CHECK(bool(nav["places_connected"]));
	CHECK(Array(nav["unreached"]).is_empty());
	NavigationRegion3D *region = Object::cast_to<NavigationRegion3D>(city->get_node_or_null(NodePath("Navigation")));
	REQUIRE(region);
	CHECK(region->get_owner() == root);
	const Ref<NavigationMesh> nm = region->get_navigation_mesh();
	REQUIRE(nm.is_valid());
	CHECK(nm->get_polygon_count() == int(nav["polygons"]));

	// No walkable polygon inside a building: test every polygon centre against every
	// building's collision footprint (shrunk by 5 cm for edge rounding).
	Node *body = city->get_node_or_null(NodePath("CityBody"));
	REQUIRE(body);
	const Vector<Vector3> verts = nm->get_vertices();
	int buildings = 0;
	int inside = 0;
	for (int i = 0; i < body->get_child_count(); i++) {
		CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i));
		if (!cs || !String(cs->get_name()).begins_with("Building")) {
			continue;
		}
		const Ref<ConvexPolygonShape3D> shape = cs->get_shape();
		REQUIRE(shape.is_valid());
		Vector<Vector2> foot;
		Vector2 c;
		for (const Vector3 &p : shape->get_points()) {
			if (p.y < 1.0f) {
				foot.push_back(Vector2(p.x, p.z));
				c += Vector2(p.x, p.z);
			}
		}
		REQUIRE(foot.size() >= 3);
		c /= foot.size();
		for (Vector2 &f : foot) {
			f = c + (f - c).normalized() * MAX(0.0f, (f - c).length() - 0.05f);
		}
		buildings++;
		for (int k = 0; k < nm->get_polygon_count(); k++) {
			const Vector<int> poly = nm->get_polygon(k);
			Vector3 m;
			for (int idx : poly) {
				m += verts[idx];
			}
			m /= poly.size();
			if (Geometry2D::is_point_in_polygon(Vector2(m.x, m.z), foot)) {
				inside++;
			}
		}
	}
	CHECK(buildings > 0);
	CHECK(inside == 0);

	// Without navigation, no region and no report.
	s["navigation"] = false;
	Node3D *root2 = memnew(Node3D);
	const Dictionary r2 = UZUWorldGen::generate_city(root2, s);
	REQUIRE(bool(r2["ok"]));
	CHECK_FALSE(r2.has("navigation"));
	CHECK(root2->get_node(NodePath(String(r2["node"])))->get_node_or_null(NodePath("Navigation")) == nullptr);
	memdelete(root);
	memdelete(root2);
}

} // namespace TestUZUWorldGen
