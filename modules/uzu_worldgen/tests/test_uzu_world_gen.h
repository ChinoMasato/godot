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
#include "scene/resources/material.h"
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

static int mesh_triangles(const Ref<Mesh> &p_mesh) {
	if (p_mesh.is_null()) {
		return 0;
	}
	int tris = 0;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		const Array arrays = p_mesh->surface_get_arrays(s);
		const Array index = arrays[Mesh::ARRAY_INDEX];
		if (!index.is_empty()) {
			tris += index.size() / 3;
		} else {
			tris += Array(arrays[Mesh::ARRAY_VERTEX]).size() / 3;
		}
	}
	return tris;
}

// Recount of a generated city's triangles from the scene itself, following the
// re-read audit (B-1511): every mesh node plus the MultiMeshes whose instances
// the generator's total includes (the window frames). Scatter MultiMeshes
// (street trees) are counted as instances, not triangles, and stay out.
static int recount_city_triangles(Node *p_city) {
	int tris = 0;
	TypedArray<Node> meshes = p_city->find_children("*", "MeshInstance3D", true, false);
	for (int i = 0; i < meshes.size(); i++) {
		tris += mesh_triangles(Object::cast_to<MeshInstance3D>(meshes[i])->get_mesh());
	}
	TypedArray<Node> mms = p_city->find_children("*", "MultiMeshInstance3D", true, false);
	for (int i = 0; i < mms.size(); i++) {
		MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(mms[i]);
		if (String(mmi->get_name()).begins_with("StreetTrees")) {
			continue;
		}
		const Ref<MultiMesh> mm = mmi->get_multimesh();
		if (mm.is_valid()) {
			tris += mm->get_instance_count() * mesh_triangles(mm->get_mesh());
		}
	}
	return tris;
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

TEST_CASE("[SceneTree][UZUWorldGen] City ground option and bench collision (B-1508)") {
	Dictionary s;
	s["preset"] = "village";
	s["size"] = 48.0;
	s["block"] = 16.0;
	s["seed"] = 1;
	// The option resolves, on by default (existing cities keep their plane).
	const Dictionary resolved = UZUWorldGen::resolve("city", s);
	REQUIRE(bool(resolved["ok"]));
	CHECK(bool(Dictionary(resolved["options"])["ground"]));

	// Default: the ground plane is there, as a mesh and as a shape, and
	// every bench placed also got a collision shape.
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_city(root, s);
	REQUIRE(bool(r["ok"]));
	Node *city = root->get_node(NodePath(String(r["node"])));
	CHECK(city->get_node_or_null(NodePath("Ground")) != nullptr);
	Node *body = city->get_node_or_null(NodePath("CityBody"));
	REQUIRE(body);
	int bench_shapes = 0;
	bool ground_shape = false;
	for (int i = 0; i < body->get_child_count(); i++) {
		const String child_name = body->get_child(i)->get_name();
		if (child_name == "Ground") {
			ground_shape = true;
		}
		if (child_name.begins_with("Bench")) {
			bench_shapes++;
		}
	}
	CHECK(ground_shape);
	CHECK(bench_shapes > 0);
	CHECK(bench_shapes == int(Dictionary(r["counts"])["benches"]));

	// ground=false (the city stands on a nature map): no ground mesh, no
	// ground shape; buildings, benches and the navigation base stay.
	s["ground"] = false;
	Node3D *root2 = memnew(Node3D);
	const Dictionary r2 = UZUWorldGen::generate_city(root2, s);
	REQUIRE(bool(r2["ok"]));
	Node *city2 = root2->get_node(NodePath(String(r2["node"])));
	CHECK(city2->get_node_or_null(NodePath("Ground")) == nullptr);
	Node *body2 = city2->get_node_or_null(NodePath("CityBody"));
	REQUIRE(body2);
	int bench_shapes2 = 0;
	bool ground_shape2 = false;
	bool building_shape2 = false;
	for (int i = 0; i < body2->get_child_count(); i++) {
		const String child_name = body2->get_child(i)->get_name();
		if (child_name == "Ground") {
			ground_shape2 = true;
		}
		if (child_name.begins_with("Bench")) {
			bench_shapes2++;
		}
		if (child_name.begins_with("Building")) {
			building_shape2 = true;
		}
	}
	CHECK_FALSE(ground_shape2);
	CHECK(bench_shapes2 == bench_shapes);
	CHECK(building_shape2);
	REQUIRE(r2.has("navigation"));
	CHECK(bool(Dictionary(r2["navigation"])["places_connected"]));
	memdelete(root);
	memdelete(root2);
}

TEST_CASE("[SceneTree][UZUWorldGen] Buildings from parts: the same layout as plain boxes, framed windows, textured materials") {
	Dictionary s;
	s["preset"] = "village";
	s["size"] = 96;
	s["seed"] = 6;
	s["detail"] = "simple";
	Node3D *plain_root = memnew(Node3D);
	const Dictionary plain = UZUWorldGen::generate_city(plain_root, s);
	s["detail"] = "high";
	Node3D *root = memnew(Node3D);
	const Dictionary r = UZUWorldGen::generate_city(root, s);
	REQUIRE(bool(plain["ok"]));
	REQUIRE(bool(r["ok"]));
	const Dictionary pc = plain["counts"], rc = r["counts"];
	// Details have their own random numbers: the same lots, buildings and windows.
	CHECK(int(pc["buildings"]) == int(rc["buildings"]));
	CHECK(int(pc["street_trees"]) == int(rc["street_trees"]));
	CHECK(Dictionary(plain["places"]) == Dictionary(r["places"]));
	CHECK_FALSE(pc.has("parts"));
	CHECK(int(r["triangles"]) > int(plain["triangles"]) * 4);

	const Dictionary parts = rc["parts"];
	const int buildings = rc["buildings"];
	CHECK(int(parts["plinths"]) == buildings);
	CHECK(int(parts["doors"]) == buildings);
	CHECK(int(parts["chimneys"]) > 0); // village roofs are gables
	CHECK(int(parts["window_frames"]) == int(rc["windows"]));

	Node *city = root->get_node(NodePath(String(r["node"])));
	// One frame mesh repeated by a MultiMesh per block, fading out far away.
	int frames = 0;
	Ref<Mesh> frame_mesh;
	TypedArray<Node> mms = city->find_children("WindowFrames_*", "MultiMeshInstance3D", false, false);
	REQUIRE(mms.size() > 0);
	for (int i = 0; i < mms.size(); i++) {
		MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(mms[i]);
		frames += mmi->get_multimesh()->get_instance_count();
		CHECK(mmi->get_visibility_range_end() > 0);
		if (frame_mesh.is_null()) {
			frame_mesh = mmi->get_multimesh()->get_mesh();
		}
		CHECK(mmi->get_multimesh()->get_mesh() == frame_mesh);
	}
	CHECK(frames == int(parts["window_frames"]));

	// Walls, roofs and trim carry a texture set (color, normal, roughness) and the UVs and
	// tangents a normal map needs.
	for (const char *name : { "Buildings", "Roofs", "BuildingsTrim", "BuildingsStone" }) {
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(city->get_node_or_null(NodePath(name)));
		REQUIRE_MESSAGE(mi, name);
		const Ref<StandardMaterial3D> m = mi->get_mesh()->surface_get_material(0);
		REQUIRE(m.is_valid());
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid());
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_NORMAL).is_valid());
		CHECK(m->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS).is_valid());
		CHECK(m->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING));
		const uint64_t format = mi->get_mesh()->surface_get_format(0);
		CHECK((format & Mesh::ARRAY_FORMAT_TEX_UV) != 0);
		CHECK((format & Mesh::ARRAY_FORMAT_TANGENT) != 0);
	}
	// The tiles wrap: the first and last columns of the roof's height (normal map) agree.
	{
		MeshInstance3D *roofs = Object::cast_to<MeshInstance3D>(city->get_node_or_null(NodePath("Roofs")));
		const Ref<StandardMaterial3D> m = roofs->get_mesh()->surface_get_material(0);
		const Ref<Image> img = m->get_texture(BaseMaterial3D::TEXTURE_NORMAL)->get_image();
		REQUIRE(img.is_valid());
		CHECK(img->has_mipmaps());
		float diff = 0;
		for (int y = 0; y < img->get_height(); y++) {
			diff += Math::abs(img->get_pixel(0, y).g - img->get_pixel(img->get_width() - 1, y).g);
		}
		CHECK(diff / img->get_height() < 0.15f);
	}
	// Walls stay inside the lots: every vertex of the detailed walls is on or above the curb.
	const PackedVector3Array v = mesh_vertices(city, "Buildings");
	REQUIRE(v.size() > 0);
	bool above = true;
	for (int i = 0; i < v.size(); i++) {
		above = above && v[i].y >= 0.149f;
	}
	CHECK(above);

	// Without textures: the same parts with plain vertex-color materials.
	s["textures"] = false;
	Node3D *root3 = memnew(Node3D);
	const Dictionary r3 = UZUWorldGen::generate_city(root3, s);
	REQUIRE(bool(r3["ok"]));
	CHECK(int(r3["triangles"]) == int(r["triangles"]));
	MeshInstance3D *walls3 = Object::cast_to<MeshInstance3D>(root3->get_node(NodePath(String(r3["node"])))->get_node_or_null(NodePath("Buildings")));
	REQUIRE(walls3);
	const Ref<StandardMaterial3D> m3 = walls3->get_mesh()->surface_get_material(0);
	CHECK(m3->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_null());
	CHECK(Array(Dictionary(r3["buildings_detail"])["textures"]).is_empty());
	memdelete(plain_root);
	memdelete(root);
	memdelete(root3);
}

TEST_CASE("[SceneTree][UZUWorldGen] City: the reported triangle count matches the meshes in the scene (B-1512)") {
	// The generator's own count is what the asset registry (B-1509) reports, so
	// it must agree with a recount from the scene itself. The B-1511 audit found
	// the window frames divided by 3 twice (get_faces() already counts
	// triangles): 6746 reported against 12272 real in the sample village.
	for (const char *detail : { "high", "simple" }) {
		INFO(detail);
		Dictionary s;
		s["preset"] = "village";
		s["size"] = 96;
		s["seed"] = 6;
		s["detail"] = detail;
		Node3D *root = memnew(Node3D);
		const Dictionary r = UZUWorldGen::generate_city(root, s);
		REQUIRE(bool(r["ok"]));
		Node *city = root->get_node(NodePath(String(r["node"])));
		CHECK(int(r["triangles"]) == recount_city_triangles(city));
		memdelete(root);
	}
}

} // namespace TestUZUWorldGen
