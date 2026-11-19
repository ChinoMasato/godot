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
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"
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

static float distance_to_path(const Array &p_points, const Vector2 &p_p) {
	float best = 1e20f;
	for (int i = 0; i + 1 < p_points.size(); i++) {
		const Array a = p_points[i], b = p_points[i + 1];
		const Vector2 a2 = Vector2(double(a[0]), double(a[2])), b2 = Vector2(double(b[0]), double(b[2]));
		const Vector2 ab = b2 - a2;
		const float t = CLAMP((p_p - a2).dot(ab) / MAX(ab.length_squared(), 1e-6f), 0.0f, 1.0f);
		best = MIN(best, p_p.distance_to(a2 + ab * t));
	}
	return best;
}

TEST_CASE("[SceneTree][UZUWorldGen] Nature: villages on flat ground, all joined by roads") {
	Dictionary s;
	s["size"] = 320;
	s["seed"] = 7;
	s["villages"] = 3;
	s["trees"] = 3.0;
	s["position"] = Vector3(10, 0, -5);
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_nature(root, s);
	REQUIRE(bool(r["ok"]));
	Node3D *nature = Object::cast_to<Node3D>(root->get_child(0));
	const Dictionary counts = r["counts"];
	CHECK(int(counts["villages"]) >= 2);
	CHECK(int(counts["village_buildings"]) > 0);
	const Array villages = Dictionary(r["places"])["villages"];
	CHECK(villages.size() == int(counts["villages"]));

	// Each village stands on leveled ground: the terrain under its plaza is at its height.
	for (int v = 0; v < villages.size(); v++) {
		const Array p = Dictionary(villages[v])["position"];
		for (float dx = -15; dx <= 15; dx += 7.5f) {
			CHECK(UZUWorldGen::height_at(nature, double(p[0]) + dx, double(p[2]) - dx * 0.5f) == doctest::Approx(double(p[1])).epsilon(0.001));
		}
		CHECK(nature->get_node_or_null(NodePath(vformat("Village%d/Buildings", v))) != nullptr);
		CHECK(nature->get_node_or_null(NodePath(vformat("Village%d/Ground", v))) == nullptr);
	}

	// One road per village (a tree), connected, continuous, dry and on the ground.
	REQUIRE(r.has("roads"));
	const Dictionary roads = r["roads"];
	CHECK(bool(roads["connected"]));
	CHECK(int(roads["count"]) == villages.size());
	const Array paths = roads["paths"];
	const float wy = r["water_y"].get_type() == Variant::NIL ? -1e20f : float(double(r["water_y"]));
	Vector<Vector2> stops;
	const Array spawn = Dictionary(Dictionary(r["places"])["spawn"])["position"];
	stops.push_back(Vector2(double(spawn[0]), double(spawn[2])));
	for (int v = 0; v < villages.size(); v++) {
		const Array p = Dictionary(villages[v])["position"];
		stops.push_back(Vector2(double(p[0]), double(p[2])));
	}
	// Union of the stops through the road ends.
	Vector<int> parent;
	for (int i = 0; i < stops.size(); i++) {
		parent.push_back(i);
	}
	auto root_of = [&](int p_i) {
		while (parent[p_i] != p_i) {
			p_i = parent[p_i];
		}
		return p_i;
	};
	auto stop_at = [&](const Array &p_point) {
		const Vector2 q = Vector2(double(p_point[0]), double(p_point[2]));
		// The spawn itself, or the edge street of a village (roads stop at a village's outer ring).
		if (stops[0].distance_to(q) < 4) {
			return 0;
		}
		for (int i = 1; i < stops.size(); i++) {
			const Vector2 d = (stops[i] - q).abs();
			if (MAX(d.x, d.y) < 72 * 0.5f + 3) {
				return i;
			}
		}
		return -1;
	};
	bool continuous = true, dry = true, grounded = true;
	for (int k = 0; k < paths.size(); k++) {
		const Array points = paths[k];
		REQUIRE(points.size() >= 2);
		const int a = stop_at(points[0]), b = stop_at(points[points.size() - 1]);
		CHECK(a >= 0);
		CHECK(b >= 0);
		if (a >= 0 && b >= 0) {
			parent.write[root_of(a)] = root_of(b);
		}
		for (int i = 0; i < points.size(); i++) {
			const Array p = points[i];
			dry = dry && double(p[1]) > wy;
			grounded = grounded && Math::abs(double(p[1]) - UZUWorldGen::height_at(nature, double(p[0]), double(p[2]))) < 0.35;
			if (i > 0) {
				const Array q = points[i - 1];
				continuous = continuous && Vector2(double(p[0]) - double(q[0]), double(p[2]) - double(q[2])).length() < 8;
			}
		}
	}
	CHECK(continuous);
	CHECK(dry);
	CHECK(grounded);
	for (int i = 1; i < stops.size(); i++) {
		CHECK(root_of(i) == root_of(0));
	}

	// No tree on a road.
	bool clear = true;
	for (const char *name : { "TreesBroadleaf", "TreesConifer" }) {
		MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(nature->get_node_or_null(NodePath(name)));
		if (!mmi || mmi->get_multimesh().is_null()) {
			continue;
		}
		// The positions from the buffer (12 floats per tree, the origin at 3, 7 and 11): headless
		// servers keep the buffer but not the per-instance transforms.
		const Vector<float> buffer = mmi->get_multimesh()->get_buffer();
		REQUIRE(buffer.size() >= 12);
		for (int i = 0; i + 11 < buffer.size(); i += 12) {
			const Vector3 t = nature->get_position() + Vector3(buffer[i + 3], buffer[i + 7], buffer[i + 11]);
			for (int k = 0; k < paths.size(); k++) {
				clear = clear && distance_to_path(paths[k], Vector2(t.x, t.z)) > 1.0f;
			}
		}
	}
	CHECK(clear);

	// Without villages there are no roads and the map is as before.
	s["villages"] = 0;
	Node3D *root2 = memnew(Node3D);
	const Dictionary r2 = UZUWorldGen::generate_nature(root2, s);
	CHECK_FALSE(r2.has("roads"));
	CHECK_FALSE(Dictionary(r2["places"]).has("villages"));
	memdelete(root);
	memdelete(root2);
}

TEST_CASE("[SceneTree][UZUWorldGen] City: benches, and a navigation mesh that joins the streets and stays out of buildings") {
	Dictionary s;
	s["size"] = 120;
	s["block"] = 40;
	s["road_width"] = 8;
	s["sidewalk"] = 2.5;
	s["seed"] = 4;
	s["roof"] = "flat";
	s["density"] = 1.0;
	s["benches"] = 15;
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_city(root, s);
	REQUIRE(bool(r["ok"]));
	Node *city = root->get_child(0);
	CHECK(int(Dictionary(r["counts"])["benches"]) > 8);
	CHECK(mesh_vertices(city, "Benches").size() > 0);
	CHECK(city->get_node_or_null(NodePath("CityBody/Bench0")) != nullptr);

	REQUIRE(r.has("navigation"));
	NavigationRegion3D *region = Object::cast_to<NavigationRegion3D>(city->get_node_or_null(NodePath("Navigation")));
	REQUIRE(region);
	CHECK(region->get_owner() == root);
	Ref<NavigationMesh> nav = region->get_navigation_mesh();
	REQUIRE(nav.is_valid());
	const int polys = nav->get_polygon_count();
	CHECK(polys > 0);
	CHECK(int(Dictionary(r["navigation"])["polygons"]) == polys);
	if (polys == 0) {
		memdelete(root);
		return;
	}

	// Flat roofs are the footprints: no walkable polygon inside one.
	const PackedVector3Array bv = mesh_vertices(city, "Buildings");
	Vector<Vector3> roof_tris;
	for (int i = 0; i + 2 < bv.size(); i += 3) {
		const Vector3 n = (bv[i + 2] - bv[i]).cross(bv[i + 1] - bv[i]);
		if (bv[i].y > 1.0f && Math::abs(n.normalized().y) > 0.99f) {
			roof_tris.push_back(bv[i]);
			roof_tris.push_back(bv[i + 1]);
			roof_tris.push_back(bv[i + 2]);
		}
	}
	REQUIRE(roof_tris.size() > 0);
	const Vector<Vector3> nv = nav->get_vertices();
	Vector<Vector3> centers;
	bool outside = true, low = true;
	for (int i = 0; i < polys; i++) {
		const Vector<int> poly = nav->get_polygon(i);
		Vector3 c;
		for (int k : poly) {
			c += nv[k];
		}
		c /= poly.size();
		centers.push_back(c);
		low = low && c.y < 1.2f;
		for (int t = 0; t < roof_tris.size(); t += 3) {
			outside = outside && !Geometry2D::is_point_in_triangle(Vector2(c.x, c.z), Vector2(roof_tris[t].x, roof_tris[t].z), Vector2(roof_tris[t + 1].x, roof_tris[t + 1].z), Vector2(roof_tris[t + 2].x, roof_tris[t + 2].z));
		}
	}
	CHECK(low);
	CHECK(outside);

	// An AI can walk from the spawn to every gate and the plaza: their polygons are linked.
	Vector<int> parent;
	for (int i = 0; i < polys; i++) {
		parent.push_back(i);
	}
	auto root_of = [&](int p_i) {
		while (parent[p_i] != p_i) {
			p_i = parent[p_i];
		}
		return p_i;
	};
	HashMap<int, int> first_poly_of_vertex;
	for (int i = 0; i < polys; i++) {
		for (int k : nav->get_polygon(i)) {
			if (first_poly_of_vertex.has(k)) {
				parent.write[root_of(i)] = root_of(first_poly_of_vertex[k]);
			} else {
				first_poly_of_vertex[k] = i;
			}
		}
	}
	auto poly_near = [&](const Array &p_pos) {
		const Vector3 p = Vector3(double(p_pos[0]), double(p_pos[1]), double(p_pos[2]));
		int best = 0;
		for (int i = 1; i < centers.size(); i++) {
			best = centers[i].distance_to(p) < centers[best].distance_to(p) ? i : best;
		}
		return best;
	};
	const Dictionary places = r["places"];
	const int spawn = root_of(poly_near(Dictionary(places["spawn"])["position"]));
	for (const char *name : { "gate_north", "gate_south", "gate_east", "gate_west", "plaza" }) {
		CHECK(root_of(poly_near(Dictionary(places[name])["position"])) == spawn);
	}

	// navigation: false skips it; ground: false leaves the ground plane out.
	s["navigation"] = false;
	s["ground"] = false;
	Node3D *root2 = memnew(Node3D);
	const Dictionary r2 = UZUWorldGen::generate_city(root2, s);
	CHECK_FALSE(r2.has("navigation"));
	CHECK(root2->get_child(0)->get_node_or_null(NodePath("Ground")) == nullptr);
	CHECK(root2->get_child(0)->get_node_or_null(NodePath("CityBody/Ground")) == nullptr);
	memdelete(root);
	memdelete(root2);
}

} // namespace TestUZUWorldGen
