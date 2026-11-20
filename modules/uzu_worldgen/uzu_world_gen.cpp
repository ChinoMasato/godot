/**************************************************************************/
/*  uzu_world_gen.cpp                                                     */
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

#include "uzu_world_gen.h"

#include "core/io/image.h"
#include "core/math/random_pcg.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/uzu_look.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/height_map_shape_3d.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"
#include "servers/navigation_3d/navigation_server_3d.h"

namespace {

// ---------------------------------------------------------------------------------------
// Options: one table per kind, so the same data checks a spec and explains it.

struct OptionDef {
	String key;
	String type; // int, float, bool, choice, color, vec3, name
	Variant def;
	double min = 0;
	double max = 0;
	Vector<String> choices;
	String help;
};

Vector<OptionDef> nature_defs() {
	Vector<OptionDef> d;
	d.push_back({ "preset", "choice", "hills", 0, 0, { "plains", "hills", "mountains", "island", "valley", "lakes" }, "starting values for every other key; any key you give overrides them" });
	d.push_back({ "seed", "int", 1, 0, 2147483647, {}, "the same seed and options always give the same map" });
	d.push_back({ "size", "float", 256.0, 32, 2048, {}, "side of the square map in meters" });
	d.push_back({ "cell", "float", 2.0, 0.5, 16, {}, "meters between terrain vertices (smaller = finer, heavier); size/cell must be at most 512" });
	d.push_back({ "shape", "choice", "open", 0, 0, { "open", "island", "valley" }, "open: hills everywhere; island: land in the middle, sea at the edges; valley: a river along z" });
	d.push_back({ "height", "float", 30.0, 0, 600, {}, "meters from the lowest to the highest possible ground" });
	d.push_back({ "roughness", "float", 0.5, 0, 1, {}, "0 smooth rolling ground, 1 sharp ridges and peaks" });
	d.push_back({ "water", "bool", true, 0, 0, {}, "fill everything below water_level with water (lakes, sea, river)" });
	d.push_back({ "water_level", "float", 0.25, 0, 1, {}, "water surface as a fraction of height" });
	d.push_back({ "snow_line", "float", 0.85, 0, 2, {}, "snow above this fraction of height (above 1 = no snow)" });
	d.push_back({ "trees", "float", 2.0, 0, 20, {}, "trees per 100 square meters inside forests (forests leave clearings)" });
	d.push_back({ "rocks", "float", 0.4, 0, 20, {}, "rocks per 100 square meters, more on slopes" });
	d.push_back({ "grass", "float", 0.0, 0, 400, {}, "grass tufts per 100 square meters on flat dry ground" });
	d.push_back({ "collision", "bool", true, 0, 0, {}, "add a static body so characters can walk on the ground" });
	d.push_back({ "villages", "int", 0, 0, 12, {}, "village sites on flat dry ground, joined by roads that keep to gentle slopes (0 = none)" });
	d.push_back({ "road_width", "float", 3.0, 0.5, 12, {}, "width of the roads between villages in meters" });
	d.push_back({ "position", "vec3", Vector3(), 0, 0, {}, "center of the map [x, y, z]; y is the height of the lowest possible ground" });
	d.push_back({ "name", "name", "Nature", 0, 0, {}, "name of the node that holds the map" });
	d.push_back({ "grass_color", "color", Color::html("#6f8f3e"), 0, 0, {}, "ground color of meadows" });
	d.push_back({ "dirt_color", "color", Color::html("#8a7550"), 0, 0, {}, "ground color mixed into meadows" });
	d.push_back({ "rock_color", "color", Color::html("#7d7870"), 0, 0, {}, "color of steep slopes" });
	d.push_back({ "sand_color", "color", Color::html("#c8b48a"), 0, 0, {}, "color of beaches and the water bottom" });
	d.push_back({ "snow_color", "color", Color::html("#f2f4f7"), 0, 0, {}, "color above the snow line" });
	d.push_back({ "water_shallow", "color", Color::html("#3f9e93"), 0, 0, {}, "water color where it is shallow" });
	d.push_back({ "water_deep", "color", Color::html("#0b2a3e"), 0, 0, {}, "water color where it is deep" });
	return d;
}

Vector<OptionDef> city_defs() {
	Vector<OptionDef> d;
	d.push_back({ "preset", "choice", "town", 0, 0, { "village", "town", "city" }, "starting values for every other key; any key you give overrides them" });
	d.push_back({ "seed", "int", 1, 0, 2147483647, {}, "the same seed and options always give the same city" });
	d.push_back({ "size", "float", 200.0, 48, 1024, {}, "side of the square city in meters" });
	d.push_back({ "layout", "choice", "grid", 0, 0, { "grid", "organic" }, "grid: straight streets; organic: bent streets and uneven blocks" });
	d.push_back({ "block", "float", 40.0, 16, 200, {}, "meters between streets" });
	d.push_back({ "road_width", "float", 8.0, 3, 30, {}, "street width in meters" });
	d.push_back({ "sidewalk", "float", 2.5, 0, 8, {}, "sidewalk width in meters (raised 15 cm)" });
	d.push_back({ "floors_min", "int", 2, 1, 80, {}, "fewest floors of a building" });
	d.push_back({ "floors_max", "int", 5, 1, 80, {}, "most floors of a building (taller toward the center)" });
	d.push_back({ "floor_height", "float", 3.2, 2.4, 6, {}, "meters per floor" });
	d.push_back({ "density", "float", 0.85, 0, 1, {}, "share of lots with a building (empty lots get a tree)" });
	d.push_back({ "plaza", "bool", true, 0, 0, {}, "leave the central block open as a square with a landmark" });
	d.push_back({ "roof", "choice", "mixed", 0, 0, { "flat", "gable", "mixed" }, "roof shape; mixed: gable up to 3 floors, flat above" });
	d.push_back({ "windows", "bool", true, 0, 0, {}, "add windows on every floor" });
	d.push_back({ "lit_windows", "float", 0.35, 0, 1, {}, "share of windows that glow" });
	d.push_back({ "lamps", "float", 24.0, 0, 200, {}, "meters between street lamps along the streets (0 = none)" });
	d.push_back({ "max_lights", "int", 48, 0, 256, {}, "most lamps that really emit light (the rest only glow), to keep it fast" });
	d.push_back({ "street_trees", "float", 12.0, 0, 100, {}, "meters between street trees (0 = none)" });
	d.push_back({ "benches", "float", 36.0, 0, 200, {}, "meters between benches along the sidewalks (0 = none); the plaza gets benches around its fountain" });
	d.push_back({ "navigation", "bool", true, 0, 0, {}, "bake a NavigationMesh of the streets, sidewalks and plaza (buildings, trees, lamps and benches cut out) so characters and AI players can find their way" });
	d.push_back({ "detail", "choice", "high", 0, 0, { "simple", "high" }, "high: buildings made of parts (stone plinth, string courses, corner pilasters, a door with a step, framed windows with sills, roofs with eaves, ridge caps and chimneys, or cornices, parapets and roof equipment); simple: plain boxes with flat windows (fewest triangles)" });
	d.push_back({ "textures", "bool", true, 0, 0, {}, "with detail high: give the buildings PBR texture sets made with them (plaster, brick, roof tiles, stone, concrete: color, normal and roughness maps)" });
	d.push_back({ "palette", "choice", "stone", 0, 0, { "warm", "stone", "modern", "pastel" }, "colors of walls, roofs, streets and sidewalks" });
	d.push_back({ "collision", "bool", true, 0, 0, {}, "add static bodies for the ground, sidewalks and buildings" });
	d.push_back({ "ground", "bool", true, 0, 0, {}, "lay the flat ground plane under the city (mesh and collision); false when the city stands on a nature map, whose terrain is the ground — the navigation base is baked from the streets either way" });
	d.push_back({ "position", "vec3", Vector3(), 0, 0, {}, "center of the city [x, y, z] (the city is flat at y)" });
	d.push_back({ "name", "name", "City", 0, 0, {}, "name of the node that holds the city" });
	return d;
}

Vector<OptionDef> defs_for(const String &p_kind) {
	if (p_kind == "nature") {
		return nature_defs();
	}
	if (p_kind == "city") {
		return city_defs();
	}
	return Vector<OptionDef>();
}

Dictionary presets_for(const String &p_kind) {
	Dictionary p;
	if (p_kind == "nature") {
		Dictionary plains;
		plains["height"] = 8.0;
		plains["roughness"] = 0.25;
		plains["water"] = false;
		plains["trees"] = 0.6;
		plains["rocks"] = 0.1;
		plains["grass"] = 6.0;
		plains["snow_line"] = 2.0;
		p["plains"] = plains;
		Dictionary hills;
		p["hills"] = hills; // the defaults
		Dictionary mountains;
		mountains["height"] = 140.0;
		mountains["roughness"] = 0.8;
		mountains["water_level"] = 0.1;
		mountains["trees"] = 1.6;
		mountains["rocks"] = 0.8;
		mountains["snow_line"] = 0.68;
		mountains["cell"] = 3.0;
		mountains["size"] = 384.0;
		p["mountains"] = mountains;
		Dictionary island;
		island["shape"] = "island";
		island["height"] = 40.0;
		island["water_level"] = 0.3;
		island["trees"] = 2.5;
		island["snow_line"] = 2.0;
		p["island"] = island;
		Dictionary valley;
		valley["shape"] = "valley";
		valley["height"] = 70.0;
		valley["roughness"] = 0.6;
		valley["water_level"] = 0.12;
		valley["trees"] = 2.2;
		valley["snow_line"] = 0.8;
		p["valley"] = valley;
		Dictionary lakes;
		lakes["height"] = 18.0;
		lakes["roughness"] = 0.35;
		lakes["water_level"] = 0.42;
		lakes["trees"] = 2.5;
		lakes["snow_line"] = 2.0;
		p["lakes"] = lakes;
	} else if (p_kind == "city") {
		Dictionary village;
		village["size"] = 120.0;
		village["layout"] = "organic";
		village["block"] = 30.0;
		village["road_width"] = 5.0;
		village["sidewalk"] = 1.5;
		village["floors_min"] = 1;
		village["floors_max"] = 2;
		village["density"] = 0.6;
		village["roof"] = "gable";
		village["palette"] = "warm";
		village["lamps"] = 18.0;
		village["street_trees"] = 14.0;
		p["village"] = village;
		Dictionary town;
		p["town"] = town; // the defaults
		Dictionary city;
		city["size"] = 320.0;
		city["block"] = 56.0;
		city["road_width"] = 12.0;
		city["sidewalk"] = 3.5;
		city["floors_min"] = 6;
		city["floors_max"] = 24;
		city["density"] = 0.95;
		city["plaza"] = false;
		city["roof"] = "flat";
		city["palette"] = "modern";
		city["lamps"] = 30.0;
		city["street_trees"] = 16.0;
		city["lit_windows"] = 0.45;
		p["city"] = city;
	}
	return p;
}

String keys_list(const Vector<OptionDef> &p_defs) {
	String s;
	for (int i = 0; i < p_defs.size(); i++) {
		s += (i ? ", " : "") + p_defs[i].key;
	}
	return s;
}

String range_text(const OptionDef &p_def) {
	if (p_def.type == "choice") {
		return "one of " + String(", ").join(p_def.choices);
	}
	if (p_def.type == "int" || p_def.type == "float") {
		return vformat("a number from %s to %s", String::num(p_def.min), String::num(p_def.max));
	}
	if (p_def.type == "bool") {
		return "true or false";
	}
	if (p_def.type == "color") {
		return "a color like \"#6f8f3e\"";
	}
	if (p_def.type == "vec3") {
		return "[x, y, z] in meters";
	}
	return "a node name";
}

Dictionary fail(const String &p_error, const String &p_key = String(), const String &p_hint = String()) {
	Dictionary r;
	r["ok"] = false;
	r["error"] = p_error;
	if (!p_key.is_empty()) {
		r["key"] = p_key;
	}
	if (!p_hint.is_empty()) {
		r["hint"] = p_hint;
	}
	return r;
}

// Converts one spec value; on failure r_error says why.
Variant convert_value(const OptionDef &p_def, const Variant &p_value, String &r_error) {
	const Variant::Type t = p_value.get_type();
	const bool number = t == Variant::INT || t == Variant::FLOAT;
	if (p_def.type == "int" || p_def.type == "float") {
		if (!number) {
			r_error = vformat("'%s' needs %s, got %s", p_def.key, range_text(p_def), p_value.to_json_string());
			return Variant();
		}
		const double v = p_value;
		if (p_def.type == "int" && v != Math::floor(v)) {
			r_error = vformat("'%s' needs a whole number, got %s", p_def.key, String::num(v));
			return Variant();
		}
		if (v < p_def.min || v > p_def.max) {
			r_error = vformat("'%s' is %s, it must be from %s to %s", p_def.key, String::num(v), String::num(p_def.min), String::num(p_def.max));
			return Variant();
		}
		return p_def.type == "int" ? Variant(int64_t(v)) : Variant(v);
	}
	if (p_def.type == "bool") {
		if (t != Variant::BOOL) {
			r_error = vformat("'%s' needs true or false, got %s", p_def.key, p_value.to_json_string());
			return Variant();
		}
		return p_value;
	}
	if (p_def.type == "choice") {
		if (t != Variant::STRING && t != Variant::STRING_NAME) {
			r_error = vformat("'%s' needs %s, got %s", p_def.key, range_text(p_def), p_value.to_json_string());
			return Variant();
		}
		const String s = p_value;
		if (!p_def.choices.has(s)) {
			r_error = vformat("'%s' is \"%s\", it must be %s", p_def.key, s, range_text(p_def));
			return Variant();
		}
		return s;
	}
	if (p_def.type == "color") {
		if (t == Variant::COLOR) {
			return p_value;
		}
		if (t == Variant::STRING && Color::html_is_valid(p_value)) {
			return Color::html(p_value);
		}
		r_error = vformat("'%s' needs %s, got %s", p_def.key, range_text(p_def), p_value.to_json_string());
		return Variant();
	}
	if (p_def.type == "vec3") {
		if (t == Variant::VECTOR3 || t == Variant::VECTOR3I) {
			return Vector3(p_value);
		}
		if (t == Variant::ARRAY || t == Variant::PACKED_FLOAT32_ARRAY || t == Variant::PACKED_FLOAT64_ARRAY || t == Variant::PACKED_INT32_ARRAY) {
			const Array a = p_value;
			bool ok = a.size() == 3;
			for (int i = 0; ok && i < 3; i++) {
				ok = a[i].get_type() == Variant::INT || a[i].get_type() == Variant::FLOAT;
			}
			if (ok) {
				return Vector3(double(a[0]), double(a[1]), double(a[2]));
			}
		}
		r_error = vformat("'%s' needs %s, got %s", p_def.key, range_text(p_def), p_value.to_json_string());
		return Variant();
	}
	// name
	if ((t == Variant::STRING || t == Variant::STRING_NAME) && !String(p_value).is_empty() && String(p_value).validate_node_name() == String(p_value)) {
		return String(p_value);
	}
	r_error = vformat("'%s' needs a node name (no . : @ / \" %%), got %s", p_def.key, p_value.to_json_string());
	return Variant();
}

// ---------------------------------------------------------------------------------------
// Noise: gradient noise with a quintic fade (no visible grid) and fractal sums.

inline uint32_t hash2(int p_x, int p_y, uint32_t p_seed) {
	uint32_t h = p_seed * 0x9E3779B1u ^ uint32_t(p_x) * 0x85EBCA77u ^ uint32_t(p_y) * 0xC2B2AE3Du;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

inline float grad(int p_ix, int p_iy, float p_dx, float p_dy, uint32_t p_seed) {
	const float a = float(hash2(p_ix, p_iy, p_seed) & 0xFFFF) / 65536.0f * float(Math::TAU);
	return p_dx * Math::cos(a) + p_dy * Math::sin(a);
}

inline float fade(float t) {
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float noise2(float p_x, float p_y, uint32_t p_seed) {
	const int x0 = int(Math::floor(p_x));
	const int y0 = int(Math::floor(p_y));
	const float fx = p_x - x0;
	const float fy = p_y - y0;
	const float n00 = grad(x0, y0, fx, fy, p_seed);
	const float n10 = grad(x0 + 1, y0, fx - 1, fy, p_seed);
	const float n01 = grad(x0, y0 + 1, fx, fy - 1, p_seed);
	const float n11 = grad(x0 + 1, y0 + 1, fx - 1, fy - 1, p_seed);
	const float u = fade(fx);
	const float v = fade(fy);
	return Math::lerp(Math::lerp(n00, n10, u), Math::lerp(n01, n11, u), v) * 1.41f; // about -1..1
}

float fbm(float p_x, float p_y, uint32_t p_seed, int p_octaves, float p_gain) {
	float sum = 0, amp = 1, norm = 0, f = 1;
	for (int o = 0; o < p_octaves; o++) {
		sum += amp * noise2(p_x * f, p_y * f, p_seed + uint32_t(o) * 1013u);
		norm += amp;
		amp *= p_gain;
		f *= 2.03f;
	}
	return sum / norm;
}

inline float hash01(int p_x, int p_y, uint32_t p_seed) {
	return float(hash2(p_x, p_y, p_seed) & 0xFFFFFF) / float(0x1000000);
}

// ---------------------------------------------------------------------------------------
// Meshes.

struct MeshBuilder {
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedColorArray colors;
	// With uv_scale > 0 (meters per texture tile), every face also gets planar UVs (u along the
	// face's horizontal, v down the face) and tangents, for tiling textures and normal maps.
	float uv_scale = 0;
	PackedVector2Array uvs;
	PackedFloat32Array tangents;

	void tri(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Color &p_color, const Vector3 &p_facing) {
		// Godot draws the side where the corners go clockwise.
		Vector3 n = (p_c - p_a).cross(p_b - p_a);
		if (n.length_squared() < 1e-12f) {
			return;
		}
		Vector3 b = p_b, c = p_c;
		if (n.dot(p_facing) < 0) {
			SWAP(b, c);
			n = -n;
		}
		n.normalize();
		verts.push_back(p_a);
		verts.push_back(b);
		verts.push_back(c);
		for (int i = 0; i < 3; i++) {
			normals.push_back(n);
			colors.push_back(p_color);
		}
		if (uv_scale > 0) {
			Vector3 u = Vector3(0, 1, 0).cross(n);
			if (u.length_squared() < 1e-6f) {
				u = Vector3(1, 0, 0);
			}
			u.normalize();
			const Vector3 v = n.cross(u); // up the face
			const Vector3 pts[3] = { p_a, b, c };
			for (int i = 0; i < 3; i++) {
				uvs.push_back(Vector2(pts[i].dot(u), -pts[i].dot(v)) / uv_scale);
				tangents.push_back(u.x);
				tangents.push_back(u.y);
				tangents.push_back(u.z);
				tangents.push_back(-1.0f); // the binormal (+v in the texture) runs down the face
			}
		}
	}
	// p_a..p_d go around the quad (either way).
	void quad(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_d, const Color &p_color, const Vector3 &p_facing) {
		tri(p_a, p_b, p_c, p_color, p_facing);
		tri(p_a, p_c, p_d, p_color, p_facing);
	}
	int triangles() const { return verts.size() / 3; }
	Ref<ArrayMesh> commit(const Ref<Material> &p_material) const {
		if (verts.is_empty()) {
			return Ref<ArrayMesh>();
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = verts;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_COLOR] = colors;
		if (uv_scale > 0 && uvs.size() == verts.size()) {
			arrays[Mesh::ARRAY_TEX_UV] = uvs;
			arrays[Mesh::ARRAY_TANGENT] = tangents;
		}
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		mesh->surface_set_material(0, p_material);
		return mesh;
	}
};

Ref<StandardMaterial3D> vertex_color_material(float p_roughness, bool p_two_sided = false) {
	Ref<StandardMaterial3D> m;
	m.instantiate();
	m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	m->set_roughness(p_roughness);
	if (p_two_sided) {
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	}
	return m;
}

Ref<StandardMaterial3D> glow_material(const Color &p_color, float p_energy) {
	Ref<StandardMaterial3D> m;
	m.instantiate();
	m->set_albedo(p_color);
	m->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
	m->set_emission(p_color);
	m->set_emission_energy_multiplier(p_energy);
	return m;
}

// A prism (p_cone_top = 0 makes a cone) standing on p_base.
// A box of half size p_half turned by p_yaw around y (its local z faces (sin yaw, 0, cos yaw)).
void add_box(MeshBuilder &p_mb, const Vector3 &p_center, const Vector3 &p_half, float p_yaw, const Color &p_color) {
	const Basis b(Vector3(0, 1, 0), p_yaw);
	const Vector3 ax = b.get_column(0) * p_half.x, ay = Vector3(0, p_half.y, 0), az = b.get_column(2) * p_half.z;
	auto P = [&](int p_x, int p_y, int p_z) { return p_center + ax * p_x + ay * p_y + az * p_z; };
	p_mb.quad(P(-1, 1, -1), P(1, 1, -1), P(1, 1, 1), P(-1, 1, 1), p_color, Vector3(0, 1, 0));
	p_mb.quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), p_color.darkened(0.3f), Vector3(0, -1, 0));
	p_mb.quad(P(1, -1, -1), P(1, -1, 1), P(1, 1, 1), P(1, 1, -1), p_color.darkened(0.12f), ax);
	p_mb.quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), p_color.darkened(0.12f), -ax);
	p_mb.quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), p_color.darkened(0.05f), az);
	p_mb.quad(P(-1, -1, -1), P(1, -1, -1), P(1, 1, -1), P(-1, 1, -1), p_color.darkened(0.2f), -az);
}

void add_prism(MeshBuilder &p_mb, const Vector3 &p_base, float p_radius, float p_top_radius, float p_height, int p_sides, const Color &p_color, bool p_caps = true) {
	const Vector3 top = p_base + Vector3(0, p_height, 0);
	for (int s = 0; s < p_sides; s++) {
		const float a0 = Math::TAU * s / p_sides, a1 = Math::TAU * (s + 1) / p_sides;
		const Vector3 d0(Math::cos(a0), 0, Math::sin(a0)), d1(Math::cos(a1), 0, Math::sin(a1));
		const Vector3 out = (d0 + d1).normalized();
		if (p_top_radius > 0) {
			p_mb.quad(p_base + d0 * p_radius, p_base + d1 * p_radius, top + d1 * p_top_radius, top + d0 * p_top_radius, p_color, out);
			if (p_caps) {
				p_mb.tri(top, top + d0 * p_top_radius, top + d1 * p_top_radius, p_color, Vector3(0, 1, 0));
			}
		} else {
			p_mb.tri(p_base + d0 * p_radius, p_base + d1 * p_radius, top, p_color, out + Vector3(0, 0.3f, 0));
		}
		if (p_caps) {
			p_mb.tri(p_base, p_base + d0 * p_radius, p_base + d1 * p_radius, p_color.darkened(0.2f), Vector3(0, -1, 0));
		}
	}
}

// A lumpy ball: an octahedron split twice, pushed in and out by noise (same corner, same push).
void add_blob(MeshBuilder &p_mb, const Vector3 &p_center, const Vector3 &p_radii, float p_lumps, uint32_t p_seed, const Color &p_color) {
	struct Tri {
		Vector3 a, b, c;
	};
	Vector<Tri> tris;
	const Vector3 px(1, 0, 0), nx(-1, 0, 0), py(0, 1, 0), ny(0, -1, 0), pz(0, 0, 1), nz(0, 0, -1);
	tris.push_back({ px, py, pz });
	tris.push_back({ pz, py, nx });
	tris.push_back({ nx, py, nz });
	tris.push_back({ nz, py, px });
	tris.push_back({ px, pz, ny });
	tris.push_back({ pz, nx, ny });
	tris.push_back({ nx, nz, ny });
	tris.push_back({ nz, px, ny });
	for (int level = 0; level < 2; level++) {
		Vector<Tri> next;
		for (const Tri &t : tris) {
			const Vector3 ab = (t.a + t.b).normalized(), bc = (t.b + t.c).normalized(), ca = (t.c + t.a).normalized();
			next.push_back({ t.a, ab, ca });
			next.push_back({ ab, t.b, bc });
			next.push_back({ ca, bc, t.c });
			next.push_back({ ab, bc, ca });
		}
		tris = next;
	}
	auto push = [&](const Vector3 &p_dir) {
		const float n = noise2(p_dir.x * 1.7f + p_dir.y * 0.6f, p_dir.z * 1.7f - p_dir.y * 0.9f, p_seed);
		return p_center + p_dir * p_radii * (1.0f + n * p_lumps);
	};
	for (const Tri &t : tris) {
		const Vector3 a = push(t.a), b = push(t.b), c = push(t.c);
		const float shade = 0.92f + 0.08f * noise2(t.a.x * 3.1f, t.a.z * 3.1f + t.a.y, p_seed + 9);
		p_mb.tri(a, b, c, Color(p_color.r * shade, p_color.g * shade, p_color.b * shade), (a + b + c) / 3.0f - p_center);
	}
}

Ref<ArrayMesh> broadleaf_tree_mesh() {
	MeshBuilder mb;
	add_prism(mb, Vector3(), 0.2f, 0.14f, 2.4f, 6, Color::html("#5b4330"));
	add_blob(mb, Vector3(0, 3.6f, 0), Vector3(1.9f, 1.6f, 1.9f), 0.18f, 11, Color::html("#4f7a35"));
	add_blob(mb, Vector3(0.7f, 4.6f, -0.3f), Vector3(1.2f, 1.0f, 1.2f), 0.2f, 12, Color::html("#5d8a3c"));
	return mb.commit(vertex_color_material(0.85f));
}

Ref<ArrayMesh> conifer_tree_mesh() {
	MeshBuilder mb;
	add_prism(mb, Vector3(), 0.16f, 0.12f, 1.4f, 6, Color::html("#4f3a2a"));
	const Color green = Color::html("#2f5a35");
	add_prism(mb, Vector3(0, 1.0f, 0), 1.7f, 0, 2.6f, 8, green.darkened(0.1f), false);
	add_prism(mb, Vector3(0, 2.4f, 0), 1.3f, 0, 2.4f, 8, green, false);
	add_prism(mb, Vector3(0, 3.7f, 0), 0.9f, 0, 2.3f, 8, green.lightened(0.08f), false);
	return mb.commit(vertex_color_material(0.9f));
}

Ref<ArrayMesh> rock_mesh(const Color &p_color) {
	MeshBuilder mb;
	add_blob(mb, Vector3(0, 0.25f, 0), Vector3(0.75f, 0.5f, 0.6f), 0.35f, 23, p_color);
	return mb.commit(vertex_color_material(0.95f));
}

Ref<ArrayMesh> grass_mesh(const Color &p_color) {
	MeshBuilder mb;
	for (int i = 0; i < 5; i++) {
		const float a = Math::TAU * i / 5 + 0.4f * i;
		const Vector3 d(Math::cos(a), 0, Math::sin(a));
		const Vector3 side = Vector3(-d.z, 0, d.x) * 0.05f;
		const Vector3 base = d * 0.06f;
		const Vector3 tip = d * 0.22f + Vector3(0, 0.45f + 0.08f * (i % 2), 0);
		mb.tri(base - side, base + side, tip, i % 2 ? p_color : p_color.lightened(0.12f), d);
	}
	return mb.commit(vertex_color_material(0.9f, true));
}

// Adds props as one MultiMesh (the buffer, which is also what is saved headless).
MultiMeshInstance3D *add_multimesh(Node *p_parent, Node *p_owner, const String &p_name, const Ref<Mesh> &p_mesh, const Vector<Transform3D> &p_transforms, bool p_shadows) {
	if (p_transforms.is_empty()) {
		return nullptr;
	}
	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_mesh(p_mesh);
	mm->set_instance_count(p_transforms.size());
	Vector<float> buffer;
	buffer.resize(p_transforms.size() * 12);
	float *w = buffer.ptrw();
	for (int i = 0; i < p_transforms.size(); i++) {
		const Transform3D &t = p_transforms[i];
		for (int row = 0; row < 3; row++) {
			w[i * 12 + row * 4 + 0] = t.basis.rows[row].x;
			w[i * 12 + row * 4 + 1] = t.basis.rows[row].y;
			w[i * 12 + row * 4 + 2] = t.basis.rows[row].z;
			w[i * 12 + row * 4 + 3] = t.origin[row];
		}
	}
	mm->set_buffer(buffer);
	MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
	mmi->set_name(p_name);
	mmi->set_multimesh(mm);
	mmi->set_cast_shadows_setting(p_shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	p_parent->add_child(mmi, true);
	mmi->set_owner(p_owner);
	return mmi;
}

MeshInstance3D *add_mesh(Node *p_parent, Node *p_owner, const String &p_name, const Ref<Mesh> &p_mesh, bool p_shadows = true) {
	if (p_mesh.is_null()) {
		return nullptr;
	}
	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_name(p_name);
	mi->set_mesh(p_mesh);
	mi->set_cast_shadows_setting(p_shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	p_parent->add_child(mi, true);
	mi->set_owner(p_owner);
	return mi;
}

Node3D *add_holder(Node *p_root, const Dictionary &p_opts, const String &p_kind, Node *&r_owner) {
	r_owner = p_root->get_owner() ? p_root->get_owner() : p_root;
	Node3D *holder = memnew(Node3D);
	holder->set_name(String(p_opts["name"]));
	holder->set_position(p_opts["position"]);
	Dictionary meta;
	meta["kind"] = p_kind;
	meta["options"] = p_opts;
	holder->set_meta("uzu_worldgen", meta);
	p_root->add_child(holder, true);
	holder->set_owner(r_owner);
	return holder;
}

Array vec_array(const Vector3 &p_v) {
	Array a;
	a.push_back(Math::snapped(double(p_v.x), 0.01));
	a.push_back(Math::snapped(double(p_v.y), 0.01));
	a.push_back(Math::snapped(double(p_v.z), 0.01));
	return a;
}

Dictionary place(const Vector3 &p_position, const String &p_about) {
	Dictionary d;
	d["position"] = vec_array(p_position);
	d["about"] = p_about;
	return d;
}

void mark(Ref<Image> &p_image, int p_x, int p_y, const Color &p_color, int p_r = 2) {
	for (int y = p_y - p_r; y <= p_y + p_r; y++) {
		for (int x = p_x - p_r; x <= p_x + p_r; x++) {
			if (x >= 0 && y >= 0 && x < p_image->get_width() && y < p_image->get_height()) {
				p_image->set_pixel(x, y, p_color);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------
// Nature.

struct NatureField {
	float size = 256, height = 30, roughness = 0.5f, water_level = 0.25f, snow_line = 0.85f;
	bool water = true;
	String shape = "open";
	uint32_t seed = 1;
	int octaves = 5;
	float gain = 0.5f;

	explicit NatureField(const Dictionary &p_o) {
		size = p_o["size"];
		height = p_o["height"];
		roughness = p_o["roughness"];
		water = p_o["water"];
		water_level = p_o["water_level"];
		snow_line = p_o["snow_line"];
		shape = p_o["shape"];
		seed = uint32_t(int64_t(p_o["seed"]));
		octaves = 3 + int(Math::round(roughness * 4));
		gain = 0.38f + 0.22f * roughness;
	}
	// 0..1 at local x, z (the map spans -size/2..size/2).
	float h01(float p_x, float p_z) const {
		const float s = 3.0f / size;
		float base = CLAMP(fbm(p_x * s, p_z * s, seed, octaves, gain) * 0.85f + 0.5f, 0.0f, 1.0f);
		float ridge = 1.0f - Math::abs(fbm(p_x * s * 1.3f + 17.3f, p_z * s * 1.3f - 9.1f, seed + 77, octaves, gain)) * 1.8f;
		ridge = CLAMP(ridge, 0.0f, 1.0f);
		ridge *= ridge;
		float h = Math::lerp(base, ridge * 0.9f + base * 0.1f, roughness * 0.55f);
		if (shape == "island") {
			const float d = Math::sqrt(p_x * p_x + p_z * p_z) / (size * 0.5f);
			h = h * 0.75f + 0.25f;
			h *= 1.0f - Math::smoothstep(0.35f, 0.92f, d);
		} else if (shape == "valley") {
			const float meander = Math::sin(p_z * s * 2.2f + float(seed % 97)) * size * 0.1f + fbm(p_z * s * 0.7f, 3.3f, seed + 5, 2, 0.5f) * size * 0.08f;
			const float d = Math::abs(p_x - meander) / (size * 0.5f);
			h = Math::lerp(0.02f, h * 0.85f + 0.15f, Math::smoothstep(0.03f, 0.55f, d));
		}
		return CLAMP(h, 0.0f, 1.0f);
	}
	float y(float p_x, float p_z) const { return h01(p_x, p_z) * height; }
	float water_y() const { return water ? water_level * height : -1e20f; }
	Vector3 normal(float p_x, float p_z, float p_step) const {
		return Vector3(y(p_x - p_step, p_z) - y(p_x + p_step, p_z), 2 * p_step, y(p_x, p_z - p_step) - y(p_x, p_z + p_step)).normalized();
	}
	float forest(float p_x, float p_z) const {
		return fbm(p_x * 7.0f / size + 40.0f, p_z * 7.0f / size - 12.0f, seed + 500, 3, 0.5f) * 0.9f + 0.5f;
	}
};

Color ground_color(const NatureField &p_f, const Dictionary &p_o, float p_x, float p_z, float p_y, float p_slope_deg) {
	const Color grass = p_o["grass_color"], dirt = p_o["dirt_color"], rock = p_o["rock_color"], sand = p_o["sand_color"], snow = p_o["snow_color"];
	const float detail = fbm(p_x * 0.06f, p_z * 0.06f, p_f.seed + 300, 3, 0.5f) * 0.9f + 0.5f;
	Color c = grass.lerp(dirt, Math::smoothstep(0.45f, 0.85f, detail));
	if (p_f.water) {
		const float wy = p_f.water_y();
		if (p_y < wy) {
			c = sand.lerp(sand.darkened(0.45f), CLAMP((wy - p_y) / MAX(p_f.height * 0.15f, 1.0f), 0.0f, 1.0f));
		} else {
			c = sand.lerp(c, Math::smoothstep(wy + 0.25f, wy + 1.2f, p_y));
		}
	}
	c = c.lerp(rock, Math::smoothstep(33.0f, 46.0f, p_slope_deg));
	const float h = p_f.height > 0 ? p_y / p_f.height : 0;
	if (h > p_f.snow_line) {
		c = c.lerp(snow, Math::smoothstep(p_f.snow_line, p_f.snow_line + 0.05f, h) * (1.0f - 0.7f * Math::smoothstep(35.0f, 50.0f, p_slope_deg)));
	}
	const float shade = 0.94f + 0.12f * (hash01(int(p_x * 3), int(p_z * 3), p_f.seed + 9) - 0.5f);
	return Color(c.r * shade, c.g * shade, c.b * shade);
}

} // namespace

// =======================================================================================

Dictionary UZUWorldGen::get_options(const String &p_kind) {
	Dictionary r;
	for (const OptionDef &d : defs_for(p_kind)) {
		Dictionary o;
		o["type"] = d.type;
		o["default"] = d.type == "color" ? Variant("#" + Color(d.def).to_html(false)) : (d.type == "vec3" ? Variant(vec_array(d.def)) : d.def);
		o["accepts"] = range_text(d);
		o["help"] = d.help;
		r[d.key] = o;
	}
	return r;
}

Dictionary UZUWorldGen::get_presets(const String &p_kind) {
	return presets_for(p_kind);
}

Dictionary UZUWorldGen::resolve(const String &p_kind, const Dictionary &p_spec) {
	const Vector<OptionDef> defs = defs_for(p_kind);
	if (defs.is_empty()) {
		return fail(vformat("unknown map kind \"%s\"", p_kind), "kind", "use \"nature\" or \"city\"");
	}
	const String what = p_kind == "nature" ? "a nature map" : "a city map";
	Dictionary opts;
	for (const OptionDef &d : defs) {
		opts[d.key] = d.def;
	}
	// Unknown keys first: the closest valid key is the most useful hint.
	const Array keys = p_spec.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		bool known = false;
		String best;
		float best_score = 0;
		for (const OptionDef &d : defs) {
			if (d.key == key) {
				known = true;
				break;
			}
			const float score = key.to_lower().similarity(d.key);
			if (score > best_score) {
				best_score = score;
				best = d.key;
			}
		}
		if (!known) {
			return fail(vformat("unknown key \"%s\" for %s", key, what), key,
					(best_score > 0.3f ? vformat("did you mean \"%s\"? ", best) : String()) + "keys: " + keys_list(defs));
		}
	}
	// The preset, then the spec over it.
	const Dictionary presets = presets_for(p_kind);
	const OptionDef &preset_def = defs[0];
	String error;
	if (p_spec.has("preset")) {
		const Variant v = convert_value(preset_def, p_spec["preset"], error);
		if (!error.is_empty()) {
			return fail(error, "preset", "presets: " + String(", ").join(preset_def.choices));
		}
		opts["preset"] = v;
	}
	const Dictionary preset = presets[opts["preset"]];
	const Array preset_keys = preset.keys();
	for (int i = 0; i < preset_keys.size(); i++) {
		opts[preset_keys[i]] = preset[preset_keys[i]];
	}
	for (const OptionDef &d : defs) {
		if (d.key == "preset" || !p_spec.has(d.key)) {
			continue;
		}
		const Variant v = convert_value(d, p_spec[d.key], error);
		if (!error.is_empty()) {
			return fail(error, d.key, d.help);
		}
		opts[d.key] = v;
	}
	// Checks across keys.
	if (p_kind == "nature") {
		const double cells = double(opts["size"]) / double(opts["cell"]);
		if (cells > 512) {
			return fail(vformat("size %s with cell %s gives %d cells per side, the limit is 512", String::num(opts["size"]), String::num(opts["cell"]), int(Math::ceil(cells))), "cell",
					vformat("raise \"cell\" to at least %s or lower \"size\"", String::num(Math::snapped(double(opts["size"]) / 512.0 + 0.005, 0.01))));
		}
	} else {
		if (int64_t(opts["floors_min"]) > int64_t(opts["floors_max"])) {
			return fail(vformat("floors_min %d is above floors_max %d", int64_t(opts["floors_min"]), int64_t(opts["floors_max"])), "floors_min", "make floors_min at most floors_max");
		}
		const double inner = double(opts["block"]) - double(opts["road_width"]) - 2 * double(opts["sidewalk"]);
		if (inner < 6) {
			return fail(vformat("block %s leaves only %s m for buildings after the road and sidewalks", String::num(opts["block"]), String::num(Math::snapped(inner, 0.1))), "block",
					vformat("raise \"block\" to at least %s, or narrow \"road_width\" / \"sidewalk\"", String::num(double(opts["road_width"]) + 2 * double(opts["sidewalk"]) + 6)));
		}
		if (double(opts["block"]) > double(opts["size"]) / 2) {
			return fail(vformat("block %s is more than half of size %s (fewer than 2 blocks per side)", String::num(opts["block"]), String::num(opts["size"])), "block", "lower \"block\" or raise \"size\"");
		}
	}
	Dictionary r;
	r["ok"] = true;
	r["options"] = opts;
	return r;
}

// ---------------------------------------------------------------------------------------

Dictionary UZUWorldGen::generate_nature(Node *p_root, const Dictionary &p_spec) {
	if (!p_root) {
		return fail("no root node to build under");
	}
	const Dictionary resolved = resolve("nature", p_spec);
	if (!bool(resolved["ok"])) {
		return resolved;
	}
	const Dictionary o = resolved["options"];
	const NatureField f(o);
	const float size = f.size;
	const int n = MAX(2, int(Math::round(size / double(o["cell"]))));
	const float cell = size / n;
	const float half = size * 0.5f;
	const float wy = f.water_y();
	Array hints;

	Node *owner = nullptr;
	Node3D *holder = add_holder(p_root, o, "nature", owner);

	// Terrain: heights on the grid, smooth normals, colors by height, slope and water.
	const int w = n + 1;
	Vector<float> heights;
	heights.resize(w * w);
	float min_y = 1e20f, max_y = -1e20f;
	int under = 0;
	for (int j = 0; j < w; j++) {
		for (int i = 0; i < w; i++) {
			const float y = f.y(-half + i * cell, -half + j * cell);
			heights.write[j * w + i] = y;
			min_y = MIN(min_y, y);
			max_y = MAX(max_y, y);
			under += y < wy ? 1 : 0;
		}
	}
	auto H = [&](int p_i, int p_j) { return heights[CLAMP(p_j, 0, n) * w + CLAMP(p_i, 0, n)]; };
	PackedVector3Array verts, normals;
	PackedColorArray colors;
	PackedInt32Array indices;
	verts.resize(w * w);
	normals.resize(w * w);
	colors.resize(w * w);
	for (int j = 0; j < w; j++) {
		for (int i = 0; i < w; i++) {
			const float x = -half + i * cell, z = -half + j * cell, y = H(i, j);
			const Vector3 nrm = Vector3(H(i - 1, j) - H(i + 1, j), 2 * cell, H(i, j - 1) - H(i, j + 1)).normalized();
			const float slope = Math::rad_to_deg(Math::acos(CLAMP(nrm.y, -1.0f, 1.0f)));
			verts.set(j * w + i, Vector3(x, y, z));
			normals.set(j * w + i, nrm);
			colors.set(j * w + i, ground_color(f, o, x, z, y, slope));
		}
	}
	indices.resize(n * n * 6);
	int k = 0;
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			const int p00 = j * w + i, p10 = p00 + 1, p01 = p00 + w, p11 = p01 + 1;
			// Clockwise from above: (p00, p10, p11) and (p00, p11, p01); height_at uses the same split.
			indices.set(k++, p00);
			indices.set(k++, p10);
			indices.set(k++, p11);
			indices.set(k++, p00);
			indices.set(k++, p11);
			indices.set(k++, p01);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> terrain;
	terrain.instantiate();
	terrain->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	terrain->surface_set_material(0, vertex_color_material(0.92f));
	add_mesh(holder, owner, "Terrain", terrain);

	if (bool(o["collision"])) {
		StaticBody3D *body = memnew(StaticBody3D);
		body->set_name("TerrainBody");
		holder->add_child(body, true);
		body->set_owner(owner);
		Ref<HeightMapShape3D> shape;
		shape.instantiate();
		shape->set_map_width(w);
		shape->set_map_depth(w);
		Vector<real_t> data;
		data.resize(w * w);
		for (int i = 0; i < w * w; i++) {
			data.write[i] = heights[i] / cell; // the shape is scaled by cell (uniform scale)
		}
		shape->set_map_data(data);
		CollisionShape3D *cs = memnew(CollisionShape3D);
		cs->set_name("Shape");
		cs->set_shape(shape);
		cs->set_scale(Vector3(cell, cell, cell));
		body->add_child(cs, true);
		cs->set_owner(owner);
	}

	if (f.water) {
		Dictionary water_opts;
		water_opts["shallow_color"] = o["water_shallow"];
		water_opts["deep_color"] = o["water_deep"];
		water_opts["clarity"] = MAX(1.0, double(f.height) * 0.08);
		// An island's sea goes on past the map so its edge is not seen.
		const float water_size = f.shape == "island" ? size * 3 : size;
		MeshInstance3D *water = UZULook::add_water(holder, Vector3(0, wy, 0), Vector2(water_size, water_size), water_opts);
		if (water) {
			water->set_name("Water");
		}
	}

	// Villages and roads (B-1504): sites on flat dry ground far apart, joined by a minimum
	// spanning tree of roads routed over the grid by A*, preferring gentle slopes and never
	// crossing water. Vegetation keeps off roads and village squares.
	const Vector3 map_origin = holder->get_position();
	auto slope_at = [&](int p_i, int p_j) {
		return Math::rad_to_deg(Math::acos(CLAMP(Vector3(H(p_i - 1, p_j) - H(p_i + 1, p_j), 2 * cell, H(p_i, p_j - 1) - H(p_i, p_j + 1)).normalized().y, -1.0f, 1.0f)));
	};
	const int village_count = int64_t(o["villages"]);
	const float road_w = double(o["road_width"]);
	Vector<int> villages; // Grid node indices (j * w + i).
	Array road_info;
	Vector<uint8_t> keep_clear; // 1 where trees, rocks and grass must not grow.
	keep_clear.resize(w * w);
	memset(keep_clear.ptrw(), 0, w * w);
	bool roads_connected = true;
	if (village_count > 0) {
		const int vstride = MAX(1, w / 64);
		Vector<int> candidates;
		for (int j = vstride; j < w - vstride; j += vstride) {
			for (int i = vstride; i < w - vstride; i += vstride) {
				const float x = -half + i * cell, z = -half + j * cell, y = H(i, j);
				if (Math::abs(x) > half * 0.85f || Math::abs(z) > half * 0.85f || y < wy + 0.8f || slope_at(i, j) > 7.0f || (f.height > 0 && y / f.height > f.snow_line - 0.05f)) {
					continue;
				}
				candidates.push_back(j * w + i);
			}
		}
		// The first site is the flattest near the center; each next one is the farthest from
		// those chosen (deterministic), while they stay far enough apart to be separate villages.
		if (!candidates.is_empty()) {
			int first = candidates[0];
			float first_score = 1e20f;
			for (int c : candidates) {
				const float x = -half + (c % w) * cell, z = -half + (c / w) * cell;
				const float score = slope_at(c % w, c / w) + Math::sqrt(x * x + z * z) / size * 30.0f;
				if (score < first_score) {
					first_score = score;
					first = c;
				}
			}
			villages.push_back(first);
			while (villages.size() < village_count) {
				int best_c = -1;
				float best_d = 0;
				for (int c : candidates) {
					float dmin = 1e20f;
					for (int v : villages) {
						dmin = MIN(dmin, Vector2((c % w) - (v % w), (c / w) - (v / w)).length() * cell);
					}
					if (dmin > best_d) {
						best_d = dmin;
						best_c = c;
					}
				}
				if (best_c < 0 || best_d < size * 0.12f) {
					hints.push_back(vformat("only %d village site(s) fit (flat dry ground far enough apart); lower \"villages\" or raise \"size\"", villages.size()));
					break;
				}
				villages.push_back(best_c);
			}
		} else {
			hints.push_back("no flat dry ground for villages: lower \"roughness\" or \"water_level\"");
		}
		// A minimum spanning tree over the sites (Prim), so every village is reachable.
		LocalVector<Vector2i> edges;
		if (villages.size() > 1) {
			Vector<bool> in_tree;
			in_tree.resize(villages.size());
			in_tree.fill(false);
			in_tree.write[0] = true;
			for (int added = 1; added < villages.size(); added++) {
				int ba = -1, bb = -1;
				float bd = 1e20f;
				for (int a = 0; a < villages.size(); a++) {
					for (int b = 0; b < villages.size(); b++) {
						if (!in_tree[a] || in_tree[b]) {
							continue;
						}
						const float d = Vector2((villages[a] % w) - (villages[b] % w), (villages[a] / w) - (villages[b] / w)).length();
						if (d < bd) {
							bd = d;
							ba = a;
							bb = b;
						}
					}
				}
				in_tree.write[bb] = true;
				edges.push_back(Vector2i(ba, bb));
			}
		}
		PackedVector3Array road_verts, road_normals;
		PackedColorArray road_colors;
		PackedInt32Array road_indices;
		const Color road_color = Color(o["dirt_color"]).darkened(0.15f);
		Vector<int> parent_of;
		parent_of.resize(w * w);
		Vector<float> g_cost;
		g_cost.resize(w * w);
		Vector<int> comp; // Union-find over the villages.
		comp.resize(villages.size());
		for (int i = 0; i < villages.size(); i++) {
			comp.write[i] = i;
		}
		auto find = [&](int p_a) {
			while (comp[p_a] != p_a) {
				p_a = comp[p_a];
			}
			return p_a;
		};
		struct Open {
			float f;
			int node;
		};
		for (const Vector2i &e : edges) {
			const int start = villages[e.x], goal = villages[e.y];
			g_cost.fill(1e30f);
			parent_of.fill(-1);
			// A* over the grid nodes (8 neighbors): the cost is the length, steeply more on
			// slopes; water and grades over 35 percent are not walkable.
			LocalVector<Open> heap;
			auto push = [&](float p_f, int p_node) {
				heap.push_back({ p_f, p_node });
				int c = heap.size() - 1;
				while (c > 0 && heap[(c - 1) / 2].f > heap[c].f) {
					SWAP(heap[(c - 1) / 2], heap[c]);
					c = (c - 1) / 2;
				}
			};
			auto pop = [&]() {
				const Open top = heap[0];
				heap[0] = heap[heap.size() - 1];
				heap.resize(heap.size() - 1);
				int c = 0;
				while (true) {
					const int l = c * 2 + 1, r = l + 1;
					int m = c;
					if (l < int(heap.size()) && heap[l].f < heap[m].f) {
						m = l;
					}
					if (r < int(heap.size()) && heap[r].f < heap[m].f) {
						m = r;
					}
					if (m == c) {
						break;
					}
					SWAP(heap[m], heap[c]);
					c = m;
				}
				return top;
			};
			const int gi = goal % w, gj = goal / w;
			auto heuristic = [&](int p_node) { return Vector2((p_node % w) - gi, (p_node / w) - gj).length() * cell; };
			g_cost.write[start] = 0;
			push(heuristic(start), start);
			bool found = false;
			while (!heap.is_empty()) {
				const Open cur = pop();
				if (cur.node == goal) {
					found = true;
					break;
				}
				if (cur.f - heuristic(cur.node) > g_cost[cur.node] + 1e-3f) {
					continue; // A stale entry.
				}
				const int ci = cur.node % w, cj = cur.node / w;
				for (int dj = -1; dj <= 1; dj++) {
					for (int di = -1; di <= 1; di++) {
						const int ni = ci + di, nj = cj + dj;
						if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni > n || nj > n) {
							continue;
						}
						const int nb = nj * w + ni;
						const float ny = H(ni, nj);
						if (ny < wy + 0.3f) {
							continue; // No roads through water.
						}
						const float len = (di != 0 && dj != 0 ? 1.41421356f : 1.0f) * cell;
						const float grade = Math::abs(ny - H(ci, cj)) / len;
						if (grade > 0.35f) {
							continue;
						}
						const float g = g_cost[cur.node] + len * (1.0f + 60.0f * grade * grade);
						if (g < g_cost[nb]) {
							g_cost.write[nb] = g;
							parent_of.write[nb] = cur.node;
							push(g + heuristic(nb), nb);
						}
					}
				}
			}
			Dictionary info;
			info["from"] = vformat("village_%d", e.x + 1);
			info["to"] = vformat("village_%d", e.y + 1);
			info["connected"] = found;
			if (!found) {
				roads_connected = false;
				road_info.push_back(info);
				continue;
			}
			comp.write[find(e.x)] = find(e.y);
			LocalVector<int> path; // From the start to the goal, as grid nodes.
			for (int node = goal; node != -1; node = parent_of[node]) {
				path.push_back(node);
			}
			path.reverse();
			float length = 0, max_grade = 0;
			Array points;
			const int clear_r = int(Math::ceil((road_w * 0.5f) / cell)) + 1;
			for (uint32_t step = 0; step < path.size(); step++) {
				const Vector3 p = verts[path[step]];
				if (step > 0) {
					const Vector3 q = verts[path[step - 1]];
					length += q.distance_to(p);
					max_grade = MAX(max_grade, Math::abs(p.y - q.y) / MAX(Vector2(p.x - q.x, p.z - q.z).length(), 0.001f));
				}
				points.push_back(vec_array(map_origin + p));
				const int pi = path[step] % w, pj = path[step] / w;
				for (int dj = -clear_r; dj <= clear_r; dj++) {
					for (int di = -clear_r; di <= clear_r; di++) {
						keep_clear.write[CLAMP(pj + dj, 0, n) * w + CLAMP(pi + di, 0, n)] = 1;
					}
				}
				// The road strip: two vertices across the direction, just above the ground.
				const Vector3 prev = verts[path[step > 0 ? step - 1 : step]];
				const Vector3 next = verts[path[step + 1 < path.size() ? step + 1 : step]];
				Vector3 dir = next - prev;
				dir.y = 0;
				dir = dir.length() > 0.0001f ? dir.normalized() : Vector3(1, 0, 0);
				const Vector3 side = Vector3(-dir.z, 0, dir.x) * (road_w * 0.5f);
				for (int sgn = -1; sgn <= 1; sgn += 2) {
					const Vector3 v = p + side * sgn;
					const float gi2 = (v.x + half) / cell, gj2 = (v.z + half) / cell;
					const int ci2 = CLAMP(int(gi2), 0, n - 1), cj2 = CLAMP(int(gj2), 0, n - 1);
					const float fx = CLAMP(gi2 - ci2, 0.0f, 1.0f), fz = CLAMP(gj2 - cj2, 0.0f, 1.0f);
					const float h00 = H(ci2, cj2), h10 = H(ci2 + 1, cj2), h01 = H(ci2, cj2 + 1), h11 = H(ci2 + 1, cj2 + 1);
					const float gy = fx >= fz ? h00 + fx * (h10 - h00) + fz * (h11 - h10) : h00 + fz * (h01 - h00) + fx * (h11 - h01);
					road_verts.push_back(Vector3(v.x, MAX(gy, p.y) + 0.08f, v.z));
					road_normals.push_back(Vector3(0, 1, 0));
					road_colors.push_back(sgn < 0 ? road_color : road_color.darkened(0.12f));
				}
				if (step > 0) {
					const int b = road_verts.size() - 4;
					for (int t : { b, b + 2, b + 3, b, b + 3, b + 1 }) {
						road_indices.push_back(t);
					}
				}
			}
			info["length"] = Math::snapped(double(length), 0.1);
			info["max_grade"] = Math::snapped(double(max_grade), 0.001);
			info["points"] = points;
			road_info.push_back(info);
		}
		for (int i = 1; i < villages.size(); i++) {
			roads_connected = roads_connected && find(i) == find(0);
		}
		if (!road_indices.is_empty()) {
			Array ra;
			ra.resize(Mesh::ARRAY_MAX);
			ra[Mesh::ARRAY_VERTEX] = road_verts;
			ra[Mesh::ARRAY_NORMAL] = road_normals;
			ra[Mesh::ARRAY_COLOR] = road_colors;
			ra[Mesh::ARRAY_INDEX] = road_indices;
			Ref<ArrayMesh> road_mesh;
			road_mesh.instantiate();
			road_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, ra);
			road_mesh->surface_set_material(0, vertex_color_material(0.95f, true));
			add_mesh(holder, owner, "Roads", road_mesh, false);
		}
		// Village squares stay clear too.
		const int vr = int(Math::ceil(10.0f / cell));
		for (int v : villages) {
			for (int dj = -vr; dj <= vr; dj++) {
				for (int di = -vr; di <= vr; di++) {
					if (di * di + dj * dj <= vr * vr) {
						keep_clear.write[CLAMP(v / w + dj, 0, n) * w + CLAMP(v % w + di, 0, n)] = 1;
					}
				}
			}
		}
	}

	// Trees, rocks and grass on a jittered grid: same seed, same places.
	Vector<Transform3D> broadleaf, conifer, rocks, grass;
	const int caps[3] = { 30000, 15000, 150000 };
	const float densities[3] = { float(o["trees"]), float(o["rocks"]), float(o["grass"]) };
	for (int kind = 0; kind < 3; kind++) {
		if (densities[kind] <= 0) {
			continue;
		}
		const float spacing = Math::sqrt(100.0f / densities[kind]);
		const int steps = int(size / spacing);
		RandomPCG rng(uint64_t(f.seed) * 7919 + kind * 104729 + 1);
		bool capped = false;
		for (int gz = 0; gz < steps && !capped; gz++) {
			for (int gx = 0; gx < steps; gx++) {
				const float x = -half + (gx + rng.randf()) * spacing;
				const float z = -half + (gz + rng.randf()) * spacing;
				const float roll = rng.randf(), yaw = rng.randf() * Math::TAU, sc = rng.randf(), tilt = rng.randf();
				if (Math::abs(x) > half - 0.5f || Math::abs(z) > half - 0.5f) {
					continue;
				}
				// Exact ground height on the triangles, like height_at.
				const float gi = (x + half) / cell, gj = (z + half) / cell;
				const int ci = CLAMP(int(gi), 0, n - 1), cj = CLAMP(int(gj), 0, n - 1);
				const float fx = gi - ci, fz = gj - cj;
				if (keep_clear[CLAMP(int(Math::round(gj)), 0, n) * w + CLAMP(int(Math::round(gi)), 0, n)]) {
					continue; // A road or a village square.
				}
				const float h00 = H(ci, cj), h10 = H(ci + 1, cj), h01 = H(ci, cj + 1), h11 = H(ci + 1, cj + 1);
				const float gy = fx >= fz ? h00 + fx * (h10 - h00) + fz * (h11 - h10) : h00 + fz * (h01 - h00) + fx * (h11 - h01);
				const Vector3 nrm = f.normal(x, z, cell);
				const float slope = Math::rad_to_deg(Math::acos(CLAMP(nrm.y, -1.0f, 1.0f)));
				const float h = f.height > 0 ? gy / f.height : 0;
				const bool dry = gy > wy + 0.4f;
				if (kind == 0) {
					if (!dry || slope > 30 || h > f.snow_line - 0.02f || roll > Math::smoothstep(0.25f, 0.5f, f.forest(x, z))) {
						continue;
					}
					// Conifers higher up (counted from the water line), broadleaf below.
					const float above = f.water ? (h - f.water_level) / MAX(1.0f - f.water_level, 0.01f) : h;
					const bool is_conifer = hash01(gx, gz, f.seed + 91) < Math::smoothstep(0.4f, 0.75f, above) + 0.08f;
					Basis b = Basis(Vector3(0, 1, 0), yaw).scaled(Vector3(1, 1, 1) * (0.75f + 0.6f * sc));
					(is_conifer ? conifer : broadleaf).push_back(Transform3D(b, Vector3(x, gy - 0.1f, z)));
				} else if (kind == 1) {
					if (gy < wy - 0.3f || roll > 0.3f + 0.7f * Math::smoothstep(12.0f, 40.0f, slope)) {
						continue;
					}
					Basis b = Basis(Vector3(0, 1, 0), yaw) * Basis(Vector3(1, 0, 0), (tilt - 0.5f) * 0.6f);
					b.scale(Vector3(0.6f + sc * 1.2f, 0.5f + tilt * 0.9f, 0.6f + roll * 1.2f));
					rocks.push_back(Transform3D(b, Vector3(x, gy - 0.15f, z)));
				} else {
					if (!dry || slope > 25 || h > f.snow_line - 0.03f || roll > 0.85f) {
						continue;
					}
					grass.push_back(Transform3D(Basis(Vector3(0, 1, 0), yaw).scaled(Vector3(1, 1, 1) * (0.7f + 0.6f * sc)), Vector3(x, gy - 0.02f, z)));
				}
				const int count = kind == 0 ? broadleaf.size() + conifer.size() : (kind == 1 ? rocks.size() : grass.size());
				if (count >= caps[kind]) {
					capped = true;
					hints.push_back(vformat("%s stopped at %d; lower \"%s\" or \"size\"", kind == 0 ? "trees" : (kind == 1 ? "rocks" : "grass"), caps[kind], kind == 0 ? "trees" : (kind == 1 ? "rocks" : "grass")));
					break;
				}
			}
		}
	}
	add_multimesh(holder, owner, "TreesBroadleaf", broadleaf_tree_mesh(), broadleaf, true);
	add_multimesh(holder, owner, "TreesConifer", conifer_tree_mesh(), conifer, true);
	add_multimesh(holder, owner, "Rocks", rock_mesh(Color(o["rock_color"]).lerp(Color(0.55f, 0.53f, 0.5f), 0.4f)), rocks, true);
	add_multimesh(holder, owner, "Grass", grass_mesh(Color(o["grass_color"]).lightened(0.05f)), grass, false);

	// Places to use next.
	Dictionary places;
	const Vector3 origin = holder->get_position();
	int peak = 0, low = 0;
	for (int i = 1; i < w * w; i++) {
		peak = heights[i] > heights[peak] ? i : peak;
		low = heights[i] < heights[low] ? i : low;
	}
	places["peak"] = place(origin + verts[peak], "the highest ground");
	if (f.water && heights[low] < wy) {
		places["water"] = place(origin + Vector3(verts[low].x, wy, verts[low].z), "on the water surface above the deepest point");
		// Shore: dry ground just above the water, nearest to the center (a beach, a dock).
		int shore = -1;
		float shore_d = 1e20f;
		for (int i = 0; i < w * w; i++) {
			const Vector3 v = verts[i];
			if (v.y > wy + 0.2f && v.y < wy + 1.5f && normals[i].y > 0.9f) {
				const float d = v.x * v.x + v.z * v.z;
				if (d < shore_d) {
					shore_d = d;
					shore = i;
				}
			}
		}
		if (shore >= 0) {
			places["shore"] = place(origin + verts[shore], "flat ground at the water's edge: a beach, a dock or a landing");
		}
	}
	// Spawn: flat dry ground near the center.
	int best = -1;
	float best_score = 1e20f;
	const int stride = MAX(1, w / 64);
	for (int j = 0; j < w; j += stride) {
		for (int i = 0; i < w; i += stride) {
			const Vector3 v = verts[j * w + i];
			const float d = Math::sqrt(v.x * v.x + v.z * v.z) / size;
			if (d > 0.35f || v.y < wy + 0.5f) {
				continue;
			}
			const float slope = Math::rad_to_deg(Math::acos(CLAMP(normals[j * w + i].y, -1.0f, 1.0f)));
			const float score = slope + d * 40.0f;
			if (score < best_score) {
				best_score = score;
				best = j * w + i;
			}
		}
	}
	if (best >= 0) {
		places["spawn"] = place(origin + verts[best], "flat dry ground near the center, a good start for the player");
	} else {
		places["spawn"] = place(origin + verts[peak], "the highest ground (no dry flat ground near the center)");
		hints.push_back("no dry flat ground near the center: lower \"water_level\" or use another seed");
	}
	// Clearings: flat dry ground with few trees, apart from each other.
	Array clearings;
	for (int j = stride; j < w - stride && clearings.size() < 3; j += stride * 3) {
		for (int i = stride; i < w - stride && clearings.size() < 3; i += stride * 3) {
			const Vector3 v = verts[j * w + i];
			const float slope = Math::rad_to_deg(Math::acos(CLAMP(normals[j * w + i].y, -1.0f, 1.0f)));
			if (v.y < wy + 0.5f || slope > 8 || f.forest(v.x, v.z) > 0.3f || (f.height > 0 && v.y / f.height > f.snow_line)) {
				continue;
			}
			bool apart = true;
			for (int c = 0; c < clearings.size(); c++) {
				const Array p = Dictionary(clearings[c])["position"];
				apart = apart && Vector2(double(p[0]) - origin.x - v.x, double(p[2]) - origin.z - v.z).length() > size / 5;
			}
			if (apart) {
				clearings.push_back(place(origin + v, "a flat open spot (few trees): room for a camp, a house or a village"));
			}
		}
	}
	places["clearings"] = clearings;
	if (!villages.is_empty()) {
		Array vs;
		for (int v = 0; v < villages.size(); v++) {
			Dictionary vp = place(origin + verts[villages[v]], "a village site: flat dry ground with a clear square, on the road network");
			vp["name"] = vformat("village_%d", v + 1);
			vs.push_back(vp);
		}
		places["villages"] = vs;
	}

	// A small top-down overview: x to the right, +z down.
	const int ow = 256;
	Ref<Image> overview = Image::create_empty(ow, ow, false, Image::FORMAT_RGB8);
	const Vector3 light = Vector3(-1, 1.4f, -1).normalized();
	for (int py = 0; py < ow; py++) {
		for (int px = 0; px < ow; px++) {
			const float x = -half + (px + 0.5f) * size / ow, z = -half + (py + 0.5f) * size / ow;
			const float y = f.y(x, z);
			const Vector3 nrm = f.normal(x, z, size / ow);
			const float slope = Math::rad_to_deg(Math::acos(CLAMP(nrm.y, -1.0f, 1.0f)));
			Color c = ground_color(f, o, x, z, y, slope);
			const float shade = CLAMP(0.55f + 0.6f * nrm.dot(light), 0.25f, 1.15f);
			c = Color(c.r * shade, c.g * shade, c.b * shade);
			if (y < wy) {
				c = c.lerp(Color(o["water_deep"]).lerp(Color(o["water_shallow"]), CLAMP(1.0f - (wy - y) / MAX(f.height * 0.15f, 1.0f), 0.0f, 1.0f)), 0.8f);
			}
			overview->set_pixel(px, py, c);
		}
	}
	auto to_px = [&](const Vector3 &p_local) { return Vector2i(int((p_local.x + half) / size * ow), int((p_local.z + half) / size * ow)); };
	for (const Vector<Transform3D> *list : { &broadleaf, &conifer }) {
		for (int i = 0; i < list->size(); i += MAX(1, list->size() / 4000)) {
			const Vector2i p = to_px((*list)[i].origin);
			mark(overview, p.x, p.y, Color(0.12f, 0.3f, 0.12f), 0);
		}
	}
	for (int c = 0; c < clearings.size(); c++) {
		const Array p = Dictionary(clearings[c])["position"];
		const Vector2i q = to_px(Vector3(double(p[0]), 0, double(p[2])) - origin);
		mark(overview, q.x, q.y, Color(1, 0.85f, 0.2f));
	}
	for (const Variant &rv : road_info) {
		const Array pts = Dictionary(rv).get("points", Array());
		for (int seg = 1; seg < pts.size(); seg++) {
			const Array a = pts[seg - 1], b = pts[seg];
			for (int t = 0; t <= 4; t++) {
				const Vector3 q = Vector3(Math::lerp(double(a[0]), double(b[0]), t / 4.0), 0, Math::lerp(double(a[2]), double(b[2]), t / 4.0)) - origin;
				const Vector2i px = to_px(q);
				mark(overview, px.x, px.y, Color(0.55f, 0.42f, 0.25f), 0);
			}
		}
	}
	for (int v : villages) {
		const Vector2i q = to_px(verts[v]);
		mark(overview, q.x, q.y, Color(0.9f, 0.5f, 0.15f), 3);
	}
	const Vector2i pk = to_px(verts[peak]);
	mark(overview, pk.x, pk.y, Color(1, 1, 1));
	if (places.has("water")) {
		const Vector2i lk = to_px(verts[low]);
		mark(overview, lk.x, lk.y, Color(0.2f, 0.5f, 1));
	}
	const Array sp = Dictionary(places["spawn"])["position"];
	const Vector2i spx = to_px(Vector3(double(sp[0]), 0, double(sp[2])) - origin);
	mark(overview, spx.x, spx.y, Color(1, 0.15f, 0.15f), 3);

	Dictionary counts;
	counts["trees"] = broadleaf.size() + conifer.size();
	counts["trees_broadleaf"] = broadleaf.size();
	counts["trees_conifer"] = conifer.size();
	counts["rocks"] = rocks.size();
	counts["grass"] = grass.size();
	Dictionary r;
	r["ok"] = true;
	r["kind"] = "nature";
	r["node"] = String(p_root->get_path_to(holder));
	r["preset"] = o["preset"];
	r["seed"] = o["seed"];
	r["size"] = size;
	r["cells"] = n;
	r["terrain_triangles"] = n * n * 2;
	Array range;
	range.push_back(Math::snapped(double(origin.y + min_y), 0.01));
	range.push_back(Math::snapped(double(origin.y + max_y), 0.01));
	r["height_range"] = range;
	r["water_y"] = f.water ? Variant(Math::snapped(double(origin.y + wy), 0.01)) : Variant();
	r["water_fraction"] = Math::snapped(double(under) / (w * w), 0.001);
	counts["villages"] = villages.size();
	counts["roads"] = road_info.size();
	r["counts"] = counts;
	r["places"] = places;
	if (village_count > 0) {
		r["roads"] = road_info;
		r["roads_connected"] = roads_connected; // Every village reachable from every other by road.
		if (!roads_connected) {
			hints.push_back("some villages could not be joined by road (water or steep ground in the way): lower \"water_level\" or \"roughness\", or use another seed");
		}
	}
	r["hints"] = hints;
	r["overview"] = overview;
	r["overview_about"] = "256x256 top-down: x to the right, +z down; red spawn, white peak, blue deepest water, yellow clearings, dark dots trees";
	Dictionary echo = o.duplicate();
	const Array ok = echo.keys();
	for (int i = 0; i < ok.size(); i++) {
		if (echo[ok[i]].get_type() == Variant::COLOR) {
			echo[ok[i]] = "#" + Color(echo[ok[i]]).to_html(false);
		} else if (echo[ok[i]].get_type() == Variant::VECTOR3) {
			echo[ok[i]] = vec_array(echo[ok[i]]);
		}
	}
	r["options"] = echo;
	return r;
}

float UZUWorldGen::height_at(Node3D *p_nature, float p_x, float p_z) {
	if (!p_nature || !p_nature->has_meta("uzu_worldgen")) {
		return p_nature ? NAN : 0.0f;
	}
	const Dictionary meta = p_nature->get_meta("uzu_worldgen");
	if (String(meta["kind"]) != "nature") {
		return NAN;
	}
	const Dictionary o = meta["options"];
	const NatureField f(o);
	const int n = MAX(2, int(Math::round(f.size / double(o["cell"]))));
	const float cell = f.size / n, half = f.size * 0.5f;
	const Vector3 pos = p_nature->get_position();
	const float x = p_x - pos.x, z = p_z - pos.z;
	if (Math::abs(x) > half || Math::abs(z) > half) {
		return NAN;
	}
	const float gi = (x + half) / cell, gj = (z + half) / cell;
	const int ci = CLAMP(int(gi), 0, n - 1), cj = CLAMP(int(gj), 0, n - 1);
	const float fx = gi - ci, fz = gj - cj;
	auto Y = [&](int p_i, int p_j) { return f.y(-half + p_i * cell, -half + p_j * cell); };
	const float h00 = Y(ci, cj), h10 = Y(ci + 1, cj), h01 = Y(ci, cj + 1), h11 = Y(ci + 1, cj + 1);
	const float gy = fx >= fz ? h00 + fx * (h10 - h00) + fz * (h11 - h10) : h00 + fz * (h01 - h00) + fx * (h11 - h01);
	return pos.y + gy;
}

// ---------------------------------------------------------------------------------------
// City.

namespace {

struct Palette {
	Vector<Color> walls, roofs;
	Color road, sidewalk, ground, flat_roof;
	Color trim = Color(0.88f, 0.85f, 0.8f); // window frames, cornices and bands (B-1219)
};

Palette palette_for(const String &p_name) {
	Palette p;
	auto cs = [](std::initializer_list<const char *> p_list) {
		Vector<Color> v;
		for (const char *s : p_list) {
			v.push_back(Color::html(s));
		}
		return v;
	};
	if (p_name == "warm") {
		p.walls = cs({ "#d8c3a0", "#c9a77c", "#e2d4b5", "#b98d64", "#d9b99a" });
		p.roofs = cs({ "#8a3b2e", "#6e4a33", "#9a5a3a", "#7a3f30" });
		p.road = Color::html("#6b6157");
		p.sidewalk = Color::html("#b9a88c");
		p.ground = Color::html("#6f8a4a");
		p.flat_roof = Color::html("#8c8478");
		p.trim = Color::html("#efe3cc");
	} else if (p_name == "modern") {
		p.walls = cs({ "#cfd3d6", "#9aa3aa", "#e6e6e1", "#6f7a84", "#b5b9bb", "#4f5b66" });
		p.roofs = cs({ "#5d6266", "#474c50" });
		p.road = Color::html("#3a3b3d");
		p.sidewalk = Color::html("#9d9fa0");
		p.ground = Color::html("#5f7550");
		p.flat_roof = Color::html("#6a6e72");
		p.trim = Color::html("#3d4349");
	} else if (p_name == "pastel") {
		p.walls = cs({ "#f0d6d0", "#d6e6d0", "#d0dcf0", "#f0ebd0", "#e6d0f0" });
		p.roofs = cs({ "#c06a5a", "#5a7ac0", "#7a9a5a", "#b0805a" });
		p.road = Color::html("#6a6a6a");
		p.sidewalk = Color::html("#d8d0c4");
		p.ground = Color::html("#7f9a5a");
		p.flat_roof = Color::html("#9a948c");
		p.trim = Color::html("#fbf6ee");
	} else { // stone
		p.walls = cs({ "#bfb6a6", "#a99f8f", "#d1c9b8", "#8f8a80", "#c4b49a" });
		p.roofs = cs({ "#4f4a48", "#6b4a3a", "#3f4d5a", "#5a3f36" });
		p.road = Color::html("#4a4a4c");
		p.sidewalk = Color::html("#a8a39a");
		p.ground = Color::html("#6b7f4c");
		p.flat_roof = Color::html("#7a766f");
		p.trim = Color::html("#ddd5c6");
	}
	return p;
}

// Moves each edge of a convex quad inward by p_d and returns the new corners. A negative p_d
// moves them outward (pass p_check = false, the shrink check would refuse it).
bool inset_quad(const Vector2 p_q[4], float p_d, Vector2 r_out[4], bool p_check = true) {
	float area = 0;
	for (int i = 0; i < 4; i++) {
		area += p_q[i].cross(p_q[(i + 1) % 4]);
	}
	const float sign = area > 0 ? 1.0f : -1.0f;
	Vector2 a[4], d[4];
	for (int i = 0; i < 4; i++) {
		const Vector2 e = (p_q[(i + 1) % 4] - p_q[i]).normalized();
		const Vector2 inward = Vector2(-e.y, e.x) * sign;
		a[i] = p_q[i] + inward * p_d;
		d[i] = e;
	}
	for (int i = 0; i < 4; i++) {
		const int prev = (i + 3) % 4;
		const float den = d[prev].cross(d[i]);
		if (Math::abs(den) < 1e-6f) {
			return false;
		}
		const float t = (a[i] - a[prev]).cross(d[i]) / den;
		r_out[i] = a[prev] + d[prev] * t;
	}
	// Still convex and the same way around?
	float area2 = 0;
	for (int i = 0; i < 4; i++) {
		area2 += r_out[i].cross(r_out[(i + 1) % 4]);
	}
	return !p_check || (area2 * area > 0 && Math::abs(area2) < Math::abs(area));
}

Vector2 bilinear(const Vector2 p_q[4], float p_u, float p_v) {
	return p_q[0].lerp(p_q[1], p_u).lerp(p_q[3].lerp(p_q[2], p_u), p_v);
}

bool in_convex(const Vector2 *p_q, int p_count, const Vector2 &p_p) {
	float sign = 0;
	for (int i = 0; i < p_count; i++) {
		const float c = (p_q[(i + 1) % p_count] - p_q[i]).cross(p_p - p_q[i]);
		if (c != 0) {
			if (sign == 0) {
				sign = c;
			} else if ((c > 0) != (sign > 0)) {
				return false;
			}
		}
	}
	return true;
}

void fill_convex(Ref<Image> &p_image, const Vector2 *p_q, int p_count, float p_half, float p_size, const Color &p_color) {
	const int ow = p_image->get_width();
	Vector2 lo(1e9, 1e9), hi(-1e9, -1e9);
	Vector<Vector2> px;
	for (int i = 0; i < p_count; i++) {
		const Vector2 p = (p_q[i] + Vector2(p_half, p_half)) / p_size * ow;
		px.push_back(p);
		lo = lo.min(p);
		hi = hi.max(p);
	}
	for (int y = MAX(0, int(lo.y)); y <= MIN(ow - 1, int(hi.y)); y++) {
		for (int x = MAX(0, int(lo.x)); x <= MIN(ow - 1, int(hi.x)); x++) {
			if (in_convex(px.ptr(), p_count, Vector2(x + 0.5f, y + 0.5f))) {
				p_image->set_pixel(x, y, p_color);
			}
		}
	}
}

void add_convex_shape(Node *p_body, Node *p_owner, const String &p_name, const Vector<Vector3> &p_points) {
	Ref<ConvexPolygonShape3D> shape;
	shape.instantiate();
	shape->set_points(p_points);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name(p_name);
	cs->set_shape(shape);
	p_body->add_child(cs, true);
	cs->set_owner(p_owner);
}

// ---------------------------------------------------------------- building recipe (B-1219)

// Tiling value noise: the pattern repeats every p_period units, so textures wrap seamlessly.
float tile_noise(float p_x, float p_y, int p_period, uint32_t p_seed) {
	const int ix = int(Math::floor(p_x)), iy = int(Math::floor(p_y));
	const float fx = p_x - ix, fy = p_y - iy;
	auto W = [&](int p_i) { return ((p_i % p_period) + p_period) % p_period; };
	const float a = hash01(W(ix), W(iy), p_seed), b = hash01(W(ix + 1), W(iy), p_seed);
	const float c = hash01(W(ix), W(iy + 1), p_seed), d = hash01(W(ix + 1), W(iy + 1), p_seed);
	const float u = fade(fx), v = fade(fy);
	return Math::lerp(Math::lerp(a, b, u), Math::lerp(c, d, u), v);
}

// p_x, p_y in [0, 1): a sum of octaves, the first with p_period cells across the tile.
float tile_fbm(float p_x, float p_y, int p_period, uint32_t p_seed, int p_octaves) {
	float sum = 0, amp = 0.5f, norm = 0;
	int period = p_period;
	for (int o = 0; o < p_octaves; o++) {
		sum += tile_noise(p_x * period, p_y * period, period, p_seed + o * 31) * amp;
		norm += amp;
		amp *= 0.5f;
		period *= 2;
	}
	return sum / norm;
}

// A PBR texture set made from three maps of one tile: brightness (multiplies the vertex
// color), height (turned into the normal map) and roughness.
struct TextureSet {
	Ref<ImageTexture> albedo, normal, roughness;
	float normal_scale = 1.0f;
};

TextureSet make_texture_set(int p_n, const Vector<float> &p_albedo, const Vector<float> &p_height, const Vector<float> &p_rough, float p_bump) {
	Vector<uint8_t> al, nm, ro;
	al.resize(p_n * p_n * 3);
	nm.resize(p_n * p_n * 3);
	ro.resize(p_n * p_n * 3);
	auto H = [&](int p_x, int p_y) { return p_height[((p_y + p_n) % p_n) * p_n + ((p_x + p_n) % p_n)]; };
	for (int y = 0; y < p_n; y++) {
		for (int x = 0; x < p_n; x++) {
			const int i = y * p_n + x;
			const uint8_t a = uint8_t(CLAMP(p_albedo[i], 0.0f, 1.0f) * 255);
			const uint8_t r = uint8_t(CLAMP(p_rough[i], 0.0f, 1.0f) * 255);
			// Image y runs down the face: a height rising downward tilts the normal up (+green).
			const float dx = (H(x + 1, y) - H(x - 1, y)) * 0.5f * p_bump, dy = (H(x, y + 1) - H(x, y - 1)) * 0.5f * p_bump;
			const Vector3 n = Vector3(-dx, dy, 1).normalized();
			for (int ch = 0; ch < 3; ch++) {
				al.write[i * 3 + ch] = a;
				ro.write[i * 3 + ch] = r;
			}
			nm.write[i * 3 + 0] = uint8_t((n.x * 0.5f + 0.5f) * 255);
			nm.write[i * 3 + 1] = uint8_t((n.y * 0.5f + 0.5f) * 255);
			nm.write[i * 3 + 2] = uint8_t((n.z * 0.5f + 0.5f) * 255);
		}
	}
	auto tex = [&](const Vector<uint8_t> &p_data, bool p_normal) {
		Ref<Image> img = Image::create_from_data(p_n, p_n, false, Image::FORMAT_RGB8, p_data);
		img->generate_mipmaps(p_normal);
		return ImageTexture::create_from_image(img);
	};
	TextureSet t;
	t.albedo = tex(al, false);
	t.normal = tex(nm, true);
	t.roughness = tex(ro, false);
	return t;
}

// The texture sets of the buildings. Each tile is p_n pixels; the meters per tile are the
// uv_scale of the MeshBuilder that uses it.
enum BuildingTexture {
	TEX_PLASTER, // 2 m: rough render with faint patches
	TEX_BRICK, // 0.75 m: 3 bricks across, 10 courses, mortar joints
	TEX_TILE, // 1.2 m: 6 barrel tiles across, 4 overlapping rows
	TEX_STONE, // 1.2 m: dressed blocks 0.6 x 0.3 m
	TEX_CONCRETE, // 2 m: flat roofs
	TEX_MAX
};

TextureSet building_texture(BuildingTexture p_kind) {
	const int n = p_kind == TEX_PLASTER || p_kind == TEX_CONCRETE ? 256 : 240;
	Vector<float> al, ht, ro;
	al.resize(n * n);
	ht.resize(n * n);
	ro.resize(n * n);
	float bump = 2.0f;
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) {
			const float u = float(x) / n, v = float(y) / n;
			const int i = y * n + x;
			const float grain = tile_fbm(u, v, 32, 7, 3);
			switch (p_kind) {
				case TEX_PLASTER: {
					const float patch = tile_fbm(u, v, 3, 11, 3);
					ht.write[i] = grain * 0.8f + patch * 0.3f;
					al.write[i] = 0.9f + 0.07f * patch + 0.03f * (grain - 0.5f);
					ro.write[i] = 0.88f + 0.08f * grain;
					bump = 1.2f;
				} break;
				case TEX_CONCRETE: {
					const float stain = tile_fbm(u, v, 4, 13, 3);
					ht.write[i] = grain * 0.8f;
					al.write[i] = 0.78f + 0.16f * stain + 0.05f * (grain - 0.5f);
					ro.write[i] = 0.9f;
					bump = 2.0f;
				} break;
				case TEX_BRICK: {
					const int cw = n / 3, rh = n / 10, mortar = 3;
					const int row = y / rh, off = (row % 2) * cw / 2;
					const int bx = (x + off) % cw, by = y % rh, col = ((x + off) / cw) % 3;
					const bool joint = bx < mortar || by < mortar;
					const float edge = MIN(MIN(bx - mortar, cw - bx), MIN(by - mortar, rh - by)) / 3.0f;
					const float brick = hash01(col, row, 21);
					ht.write[i] = joint ? 0.0f : MIN(1.0f, 0.55f + 0.45f * MIN(edge, 1.0f)) + 0.15f * grain;
					al.write[i] = joint ? 0.95f : 0.62f + 0.3f * brick + 0.06f * (grain - 0.5f);
					ro.write[i] = joint ? 0.95f : 0.8f + 0.1f * grain;
					bump = 6.0f;
				} break;
				case TEX_TILE: {
					const int tw = n / 6, rh = n / 4;
					const int row = y / rh, off = (row % 2) * tw / 2;
					const int tx = (x + off) % tw, ty = y % rh, col = ((x + off) / tw) % 6;
					const float across = Math::sin(Math::PI * float(tx) / tw); // barrel
					const float down = float(ty) / rh; // thicker toward the lower lip
					const bool gap = tx < 2;
					const float tile = hash01(col, row, 33);
					ht.write[i] = gap ? 0.0f : 0.25f + 0.45f * across + 0.3f * down + 0.08f * grain;
					al.write[i] = (gap ? 0.45f : 0.72f + 0.22f * tile) * (0.82f + 0.18f * (1.0f - down * down)) + 0.04f * (grain - 0.5f);
					ro.write[i] = 0.62f + 0.12f * grain;
					bump = 7.0f;
				} break;
				case TEX_STONE: {
					const int bw = n / 2, rh = n / 4, mortar = 3;
					const int row = y / rh, off = (row % 2) * bw / 2;
					const int bx = (x + off) % bw, by = y % rh, col = ((x + off) / bw) % 2;
					const bool joint = bx < mortar || by < mortar;
					const float edge = MIN(MIN(bx - mortar, bw - bx), MIN(by - mortar, rh - by)) / 6.0f;
					const float block = hash01(col, row, 41);
					const float chisel = tile_fbm(u, v, 8, 43, 4);
					ht.write[i] = joint ? 0.0f : MIN(1.0f, 0.4f + 0.6f * MIN(edge, 1.0f)) + 0.35f * chisel;
					al.write[i] = joint ? 0.7f : 0.72f + 0.22f * block + 0.1f * (chisel - 0.5f);
					ro.write[i] = 0.85f + 0.1f * chisel;
					bump = 5.0f;
				} break;
				default:
					break;
			}
		}
	}
	return make_texture_set(n, al, ht, ro, bump);
}

Ref<StandardMaterial3D> textured_material(const TextureSet &p_set) {
	Ref<StandardMaterial3D> m = vertex_color_material(1.0f);
	m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, p_set.albedo);
	m->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	m->set_texture(BaseMaterial3D::TEXTURE_NORMAL, p_set.normal);
	m->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, p_set.roughness);
	m->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
	m->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
	return m;
}

// A box with its edges cut at 45 degrees by p_chamfer (0 = sharp): 6 faces, 12 edge strips and
// 8 corner triangles, so the edges catch the light. p_axes holds the unit directions of the
// box's x, y and z as columns.
void add_cbox(MeshBuilder &p_mb, const Basis &p_axes, const Vector3 &p_center, const Vector3 &p_half, float p_chamfer, const Color &p_color) {
	const Vector3 X = p_axes.get_column(0), Y = p_axes.get_column(1), Z = p_axes.get_column(2);
	const float ch = MIN(p_chamfer, MIN(p_half.x, MIN(p_half.y, p_half.z)) * 0.9f);
	auto L = [&](float p_x, float p_y, float p_z) { return p_center + X * p_x + Y * p_y + Z * p_z; };
	const float hx = p_half.x, hy = p_half.y, hz = p_half.z;
	// The corner points of corner (sx, sy, sz) on its x, y and z faces.
	auto PX = [&](int sx, int sy, int sz) { return L(sx * hx, sy * (hy - ch), sz * (hz - ch)); };
	auto PY = [&](int sx, int sy, int sz) { return L(sx * (hx - ch), sy * hy, sz * (hz - ch)); };
	auto PZ = [&](int sx, int sy, int sz) { return L(sx * (hx - ch), sy * (hy - ch), sz * hz); };
	for (int s = -1; s <= 1; s += 2) {
		p_mb.quad(PX(s, -1, -1), PX(s, 1, -1), PX(s, 1, 1), PX(s, -1, 1), p_color, X * s);
		p_mb.quad(PY(-1, s, -1), PY(1, s, -1), PY(1, s, 1), PY(-1, s, 1), p_color, Y * s);
		p_mb.quad(PZ(-1, -1, s), PZ(1, -1, s), PZ(1, 1, s), PZ(-1, 1, s), p_color, Z * s);
	}
	if (ch <= 0.0005f) {
		return;
	}
	for (int a = -1; a <= 1; a += 2) {
		for (int b = -1; b <= 1; b += 2) {
			p_mb.quad(PX(a, b, -1), PY(a, b, -1), PY(a, b, 1), PX(a, b, 1), p_color, X * a + Y * b); // along z
			p_mb.quad(PY(-1, a, b), PZ(-1, a, b), PZ(1, a, b), PY(1, a, b), p_color, Y * a + Z * b); // along x
			p_mb.quad(PX(a, -1, b), PZ(a, -1, b), PZ(a, 1, b), PX(a, 1, b), p_color, X * a + Z * b); // along y
		}
	}
	for (int sx = -1; sx <= 1; sx += 2) {
		for (int sy = -1; sy <= 1; sy += 2) {
			for (int sz = -1; sz <= 1; sz += 2) {
				p_mb.tri(PX(sx, sy, sz), PY(sx, sy, sz), PZ(sx, sy, sz), p_color, X * sx + Y * sy + Z * sz);
			}
		}
	}
}

// A band around a footprint (plinth, string course, cornice): from p_y0 to p_y1, standing
// out of the walls by p_out, its upper outer edge cut by p_chamfer.
void add_ring(MeshBuilder &p_mb, const Vector2 p_fp[4], float p_y0, float p_y1, float p_out, float p_chamfer, const Color &p_color, bool p_bottom) {
	Vector2 o[4], o2[4];
	if (!inset_quad(p_fp, -p_out, o, false) || !inset_quad(p_fp, -(p_out - p_chamfer), o2, false)) {
		return;
	}
	const Vector2 c = (p_fp[0] + p_fp[1] + p_fp[2] + p_fp[3]) / 4;
	const float ym = p_y1 - p_chamfer;
	for (int i = 0; i < 4; i++) {
		const int j = (i + 1) % 4;
		const Vector2 mid = (o[i] + o[j]) / 2 - c;
		const Vector3 out(mid.x, 0, mid.y);
		auto V = [](const Vector2 &p_p, float p_y) { return Vector3(p_p.x, p_y, p_p.y); };
		p_mb.quad(V(o[i], p_y0), V(o[j], p_y0), V(o[j], ym), V(o[i], ym), p_color, out);
		if (p_chamfer > 0) {
			p_mb.quad(V(o[i], ym), V(o[j], ym), V(o2[j], p_y1), V(o2[i], p_y1), p_color, out.normalized() + Vector3(0, 1, 0));
		}
		p_mb.quad(V(p_fp[i], p_y1), V(p_fp[j], p_y1), V(o2[j], p_y1), V(o2[i], p_y1), p_color, Vector3(0, 1, 0));
		if (p_bottom) {
			p_mb.quad(V(p_fp[i], p_y0), V(p_fp[j], p_y0), V(o[j], p_y0), V(o[i], p_y0), p_color.darkened(0.1f), Vector3(0, -1, 0));
		}
	}
}

// One window's surround, drawn once and repeated by a MultiMesh: jambs, a head with a
// cornice, a sill that stands out, a mullion and a transom, and shutters for villages.
// Local x runs along the wall, y up, z out of the wall; the origin is the glass center.
Ref<ArrayMesh> window_frame_mesh(float p_ww, float p_wh, const Color &p_trim, bool p_shutters, const Color &p_shutter, const Ref<Material> &p_material, float p_uv_scale) {
	MeshBuilder mb;
	mb.uv_scale = p_uv_scale;
	const Basis I;
	const float t = 0.07f, d = 0.06f;
	add_cbox(mb, I, Vector3(-p_ww - t * 0.5f, 0, d * 0.5f), Vector3(t * 0.5f, p_wh + t, d * 0.5f), 0.0f, p_trim);
	add_cbox(mb, I, Vector3(p_ww + t * 0.5f, 0, d * 0.5f), Vector3(t * 0.5f, p_wh + t, d * 0.5f), 0.0f, p_trim);
	add_cbox(mb, I, Vector3(0, p_wh + t + 0.07f, 0.045f), Vector3(p_ww + t + 0.07f, 0.08f, 0.045f), 0.02f, p_trim);
	add_cbox(mb, I, Vector3(0, -p_wh - t * 0.5f, 0.08f), Vector3(p_ww + t + 0.09f, 0.045f, 0.08f), 0.02f, p_trim);
	add_cbox(mb, I, Vector3(0, -0.05f * p_wh, 0.02f), Vector3(0.02f, p_wh, 0.02f), 0.0f, p_trim);
	add_cbox(mb, I, Vector3(0, p_wh * 0.4f, 0.02f), Vector3(p_ww, 0.02f, 0.02f), 0.0f, p_trim);
	if (p_shutters) {
		for (int s = -1; s <= 1; s += 2) {
			add_cbox(mb, I, Vector3(s * (p_ww * 1.5f + t), 0, 0.03f), Vector3(p_ww * 0.5f, p_wh + t * 0.5f, 0.025f), 0.01f, p_shutter);
		}
	}
	return mb.commit(p_material);
}

} // namespace

Dictionary UZUWorldGen::generate_city(Node *p_root, const Dictionary &p_spec) {
	if (!p_root) {
		return fail("no root node to build under");
	}
	const Dictionary resolved = resolve("city", p_spec);
	if (!bool(resolved["ok"])) {
		return resolved;
	}
	const Dictionary o = resolved["options"];
	const float size = double(o["size"]), half = size * 0.5f;
	const float road_w = double(o["road_width"]), walk = double(o["sidewalk"]), floor_h = double(o["floor_height"]);
	const int fmin = int64_t(o["floors_min"]), fmax = int64_t(o["floors_max"]);
	const uint32_t seed = uint32_t(int64_t(o["seed"]));
	const bool organic = String(o["layout"]) == "organic";
	const String roof = o["roof"];
	const Palette pal = palette_for(o["palette"]);
	const float curb = 0.15f;
	RandomPCG rng(uint64_t(seed) * 2654435761u + 17);
	RandomPCG drng(uint64_t(seed) * 2246822519u + 101); // building details and lit windows (B-1219): the layout stays the same at every detail level
	Array hints;

	Node *owner = nullptr;
	Node3D *holder = add_holder(p_root, o, "city", owner);

	// Street graph: an (m+1) x (m+1) grid of crossings, bent for "organic".
	const int m = MAX(2, int(Math::round(size / double(o["block"]))));
	const float B = size / m;
	Vector<Vector2> nodes;
	nodes.resize((m + 1) * (m + 1));
	for (int j = 0; j <= m; j++) {
		for (int i = 0; i <= m; i++) {
			Vector2 p(-half + i * B, -half + j * B);
			if (organic) {
				const float jx = (rng.randf() - 0.5f) * 0.42f * B, jz = (rng.randf() - 0.5f) * 0.42f * B;
				p.x += (i > 0 && i < m) ? jx : 0;
				p.y += (j > 0 && j < m) ? jz : 0;
			}
			nodes.write[j * (m + 1) + i] = p;
		}
	}
	auto N = [&](int p_i, int p_j) { return nodes[p_j * (m + 1) + p_i]; };
	Vector<Vector2> edge_a, edge_b;
	for (int j = 0; j <= m; j++) {
		for (int i = 0; i <= m; i++) {
			if (i < m) {
				edge_a.push_back(N(i, j));
				edge_b.push_back(N(i + 1, j));
			}
			if (j < m) {
				edge_a.push_back(N(i, j));
				edge_b.push_back(N(i, j + 1));
			}
		}
	}

	MeshBuilder ground_mb, road_mb, block_mb, building_mb, window_mb, lit_mb, post_mb, landmark_mb;
	// B-1219: buildings made of parts, one builder per material (and texture set).
	const bool detailed = String(o["detail"]) == "high";
	const bool textured = detailed && bool(o["textures"]);
	MeshBuilder kit_wall, kit_brick, kit_stone, kit_trim, kit_roof, kit_flat;
	if (textured) {
		kit_wall.uv_scale = 2.0f;
		kit_brick.uv_scale = 0.75f;
		kit_stone.uv_scale = 1.2f;
		kit_trim.uv_scale = 1.0f;
		kit_roof.uv_scale = 1.2f;
		kit_flat.uv_scale = 2.0f;
	}
	TextureSet tex[TEX_MAX];
	if (textured) {
		for (int k = 0; k < TEX_MAX; k++) {
			tex[k] = building_texture(BuildingTexture(k));
		}
	}
	auto kit_material = [&](BuildingTexture p_kind, float p_roughness) -> Ref<Material> {
		return textured ? Ref<Material>(textured_material(tex[p_kind])) : Ref<Material>(vertex_color_material(p_roughness));
	};
	Ref<Material> trim_material = kit_material(TEX_PLASTER, 0.8f);
	Ref<ArrayMesh> frame_mesh;
	int parts_plinths = 0, parts_bands = 0, parts_pilasters = 0, parts_doors = 0, parts_chimneys = 0, parts_roof_props = 0, frames_total = 0;
	const Vector3 up(0, 1, 0);
	const float margin = 20;
	if (bool(o["ground"])) {
		ground_mb.quad(Vector3(-half - margin, 0, -half - margin), Vector3(half + margin, 0, -half - margin), Vector3(half + margin, 0, half + margin), Vector3(-half - margin, 0, half + margin), pal.ground, up);
	}
	const float ry = 0.02f;
	float road_length = 0;
	for (int e = 0; e < edge_a.size(); e++) {
		const Vector2 a = edge_a[e], b = edge_b[e];
		const Vector2 side = (b - a).normalized().orthogonal() * (road_w * 0.5f);
		road_mb.quad(Vector3(a.x + side.x, ry, a.y + side.y), Vector3(b.x + side.x, ry, b.y + side.y), Vector3(b.x - side.x, ry, b.y - side.y), Vector3(a.x - side.x, ry, a.y - side.y), pal.road, up);
		road_length += a.distance_to(b);
	}
	for (const Vector2 &p : nodes) {
		const float r = road_w * 0.75f;
		for (int s = 0; s < 8; s++) {
			const float a0 = Math::TAU * s / 8, a1 = Math::TAU * (s + 1) / 8;
			road_mb.tri(Vector3(p.x, ry, p.y), Vector3(p.x + Math::cos(a0) * r, ry, p.y + Math::sin(a0) * r), Vector3(p.x + Math::cos(a1) * r, ry, p.y + Math::sin(a1) * r), pal.road, up);
		}
	}

	StaticBody3D *body = nullptr;
	if (bool(o["collision"])) {
		body = memnew(StaticBody3D);
		body->set_name("CityBody");
		holder->add_child(body, true);
		body->set_owner(owner);
		if (bool(o["ground"])) {
			Ref<BoxShape3D> ground_shape;
			ground_shape.instantiate();
			ground_shape->set_size(Vector3(size + 2 * margin, 1, size + 2 * margin));
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_name("Ground");
			cs->set_shape(ground_shape);
			cs->set_position(Vector3(0, -0.5f, 0));
			body->add_child(cs, true);
			cs->set_owner(owner);
		}
	}

	// Blocks: a raised slab (sidewalk) with lots split into buildings.
	const int plaza_i = (m - 1) / 2, plaza_j = (m - 1) / 2;
	const bool plaza = bool(o["plaza"]);
	Vector<Transform3D> street_trees;
	int buildings = 0, windows_count = 0, lit_count = 0, empty_lots = 0;
	float tallest = 0;
	Vector3 tallest_top;
	Vector<Vector<Vector2>> footprints; // for the overview
	Vector<float> footprint_h;
	Vector<Color> footprint_c;
	Vector<Vector<Vector2>> slabs;
	Vector3 plaza_center;
	const int window_cap = 250000;
	for (int bj = 0; bj < m; bj++) {
		for (int bi = 0; bi < m; bi++) {
			const Vector2 q[4] = { N(bi, bj), N(bi + 1, bj), N(bi + 1, bj + 1), N(bi, bj + 1) };
			Vector2 slab[4], lot[4];
			if (!inset_quad(q, road_w * 0.5f, slab)) {
				continue;
			}
			Vector<Vector2> slab_v;
			for (int c = 0; c < 4; c++) {
				slab_v.push_back(slab[c]);
			}
			slabs.push_back(slab_v);
			const bool is_plaza = plaza && bi == plaza_i && bj == plaza_j;
			// The slab: top and curb sides.
			Vector3 s3[4], b3[4];
			for (int c = 0; c < 4; c++) {
				s3[c] = Vector3(slab[c].x, curb, slab[c].y);
				b3[c] = Vector3(slab[c].x, 0.0f, slab[c].y);
			}
			const Color slab_color = is_plaza ? pal.sidewalk.lightened(0.12f) : pal.sidewalk;
			block_mb.quad(s3[0], s3[1], s3[2], s3[3], slab_color, up);
			const Vector2 center2 = (slab[0] + slab[1] + slab[2] + slab[3]) / 4;
			for (int c = 0; c < 4; c++) {
				const Vector2 mid = (slab[c] + slab[(c + 1) % 4]) / 2;
				const Vector3 out = Vector3(mid.x - center2.x, 0, mid.y - center2.y);
				block_mb.quad(b3[c], b3[(c + 1) % 4], s3[(c + 1) % 4], s3[c], slab_color.darkened(0.15f), out);
			}
			if (body) {
				Vector<Vector3> pts;
				for (int c = 0; c < 4; c++) {
					pts.push_back(s3[c]);
					pts.push_back(b3[c]);
				}
				add_convex_shape(body, owner, vformat("Block_%d_%d", bi, bj), pts);
			}
			if (is_plaza) {
				plaza_center = Vector3(center2.x, curb, center2.y);
				// Landmark: a fountain basin with a column.
				add_prism(landmark_mb, plaza_center, 3.2f, 3.2f, 0.7f, 16, Color::html("#a9a49a"));
				add_prism(landmark_mb, plaza_center + Vector3(0, 0.7f, 0), 0.5f, 0.35f, 3.4f, 8, Color::html("#bdb7ab"));
				add_prism(landmark_mb, plaza_center + Vector3(0, 0.71f, 0), 2.9f, 2.9f, 0.02f, 16, Color::html("#3f7f8f"));
				for (int t = 0; t < 4; t++) {
					const Vector2 p = slab[t].lerp(center2, 0.22f);
					street_trees.push_back(Transform3D(Basis().scaled(Vector3(0.9f, 0.9f, 0.9f)), Vector3(p.x, curb, p.y)));
				}
				continue;
			}
			if (!inset_quad(slab, walk, lot)) {
				continue;
			}
			Vector<Transform3D> block_frames; // window frames of this block (one MultiMesh, so far ones can drop out)
			// Lots: strips along the longer side, two rows when deep.
			const float len_u = (lot[0].distance_to(lot[1]) + lot[3].distance_to(lot[2])) / 2;
			const float len_v = (lot[0].distance_to(lot[3]) + lot[1].distance_to(lot[2])) / 2;
			const bool along_u = len_u >= len_v;
			const float frontage = along_u ? len_u : len_v, depth = along_u ? len_v : len_u;
			// Houses get narrower and shallower in villages, wider and deeper in cities.
			const float target = fmax <= 2 ? 8.0f : (fmax <= 6 ? 10.0f : 16.0f);
			const int strips = MAX(1, int(Math::round(frontage / target)));
			const int rows = depth > (fmax <= 2 ? 14.0f : (fmax <= 6 ? 16.0f : 26.0f)) ? 2 : 1;
			const float gap = 0.6f;
			// Uneven widths: each inner cut moves up to a quarter of a strip.
			Vector<float> cuts;
			cuts.push_back(0);
			for (int s = 1; s < strips; s++) {
				cuts.push_back((s + (rng.randf() - 0.5f) * 0.5f) / strips);
			}
			cuts.push_back(1);
			for (int s = 0; s < strips; s++) {
				for (int rw = 0; rw < rows; rw++) {
					float a0 = cuts[s], a1 = cuts[s + 1];
					float b0 = float(rw) / rows, b1 = float(rw + 1) / rows;
					const float ga = gap / frontage, gb = gap / depth;
					a0 += ga;
					a1 -= ga;
					b0 += gb;
					b1 -= gb;
					auto P = [&](float p_a, float p_b) { return along_u ? bilinear(lot, p_a, p_b) : bilinear(lot, p_b, p_a); };
					Vector2 fp[4] = { P(a0, b0), P(a1, b0), P(a1, b1), P(a0, b1) };
					const Vector2 fc = (fp[0] + fp[1] + fp[2] + fp[3]) / 4;
					if (rng.randf() > float(double(o["density"]))) {
						empty_lots++;
						street_trees.push_back(Transform3D(Basis(up, rng.randf() * Math::TAU).scaled(Vector3(1, 1, 1) * (0.8f + 0.4f * rng.randf())), Vector3(fc.x, curb, fc.y)));
						continue;
					}
					// Taller toward the center.
					const float dc = CLAMP(fc.length() / half, 0.0f, 1.0f);
					const float t = CLAMP(rng.randf() * 0.65f + 0.35f * (1.0f - dc), 0.0f, 1.0f);
					const int floors = CLAMP(int(Math::round(Math::lerp(float(fmin), float(fmax), t))), fmin, fmax);
					const float h = floors * floor_h;
					const Color wall = pal.walls[rng.rand(pal.walls.size())].lerp(Color(1, 1, 1), (rng.randf() - 0.5f) * 0.1f);
					const Color roof_c = pal.roofs[rng.rand(pal.roofs.size())];
					Vector3 f0[4], f1[4];
					for (int c = 0; c < 4; c++) {
						f0[c] = Vector3(fp[c].x, curb, fp[c].y);
						f1[c] = Vector3(fp[c].x, curb + h, fp[c].y);
					}
					const Vector3 c3(fc.x, curb, fc.y);
					const bool gable_roof = roof == "gable" || (roof == "mixed" && floors <= 3);
					if (detailed) {
						// fb-1219 recipe: walls (plaster or brick), a stone plinth, string courses and
						// corner pilasters, a door with a step, framed windows (the frames are one
						// repeated part), and a roof with eaves, thickness, a ridge cap and a chimney,
						// or a cornice, a parapet and roof equipment.
						const bool brick = drng.randf() < 0.35f;
						MeshBuilder &wall_mb = brick ? kit_brick : kit_wall;
						const Color wall_c = brick ? wall.lerp(Color(0.6f, 0.34f, 0.25f), 0.55f) : wall;
						const Color trim_c = pal.trim.lerp(wall, 0.15f);
						const Color stone_c = wall.darkened(0.3f).lerp(Color(0.56f, 0.54f, 0.5f), 0.5f);
						const float yt = curb + h;
						const float plinth_h = fmax <= 2 ? 0.45f : 0.75f;
						Vector3 along_c[4], n_c[4];
						float len_c[4];
						int front = 0;
						float front_score = -1e9f;
						for (int c = 0; c < 4; c++) {
							const Vector3 a = f0[c], b = f0[(c + 1) % 4];
							len_c[c] = a.distance_to(b);
							along_c[c] = (b - a) / MAX(len_c[c], 0.001f);
							Vector3 n = Vector3(along_c[c].z, 0, -along_c[c].x);
							const Vector3 out = (a + b) / 2 - c3;
							if (n.dot(out) < 0) {
								n = -n;
							}
							n_c[c] = n;
							wall_mb.quad(f0[c], f0[(c + 1) % 4], f1[(c + 1) % 4], f1[c], wall_c, n);
							// The front faces away from the block's middle, toward the street.
							const Vector2 mid2 = (fp[c] + fp[(c + 1) % 4]) / 2;
							const float score = Vector2(n.x, n.z).dot((mid2 - center2).normalized()) + len_c[c] * 0.01f;
							if (score > front_score) {
								front_score = score;
								front = c;
							}
						}
						add_ring(kit_stone, fp, curb, curb + plinth_h, 0.06f, 0.025f, stone_c, false);
						parts_plinths++;
						if (floors >= 2 && drng.randf() < (fmax <= 2 ? 0.3f : 0.75f)) {
							for (int fl = 1; fl < floors; fl++) {
								const float y = curb + fl * floor_h - 0.12f;
								add_ring(kit_trim, fp, y - 0.07f, y + 0.07f, 0.05f, 0.02f, trim_c, true);
							}
							parts_bands++;
						}
						const bool gable = gable_roof;
						const float cornice_h = gable ? 0.0f : 0.32f;
						if (drng.randf() < 0.5f) {
							for (int c = 0; c < 4; c++) {
								const int prev = (c + 3) % 4;
								const float y0 = curb + plinth_h, y1 = yt - cornice_h;
								const Vector3 corner = f0[c] - (n_c[c] + n_c[prev]) * 0.18f;
								add_cbox(brick ? kit_stone : kit_trim, Basis(along_c[c], up, n_c[c]), Vector3(corner.x, (y0 + y1) * 0.5f, corner.z), Vector3(0.24f, (y1 - y0) * 0.5f, 0.24f), 0.04f, brick ? stone_c : trim_c);
							}
							parts_pilasters++;
						}
						// The door: in the middle of the front, on the ground floor.
						const float door_h = fmax <= 2 ? 2.1f : 2.4f, door_w = 0.55f;
						const float door_at = len_c[front] * 0.5f;
						{
							const Basis fb(along_c[front], up, n_c[front]);
							const Vector3 base = f0[front] + along_c[front] * door_at;
							MeshBuilder &door_mb = kit_trim;
							const Color wood = Color(0.33f, 0.22f, 0.15f).lerp(roof_c, 0.25f);
							const Vector3 n = n_c[front], al = along_c[front];
							door_mb.quad(base - al * door_w + n * 0.075f, base + al * door_w + n * 0.075f, base + al * door_w + n * 0.075f + up * door_h, base - al * door_w + n * 0.075f + up * door_h, wood, n);
							for (int sg = -1; sg <= 1; sg += 2) {
								add_cbox(door_mb, fb, base + al * (sg * (door_w + 0.07f)) + up * (door_h * 0.5f) + n * 0.07f, Vector3(0.07f, door_h * 0.5f, 0.07f), 0.015f, trim_c);
								// Two raised panels on the leaf.
								add_cbox(door_mb, fb, base + al * (sg * door_w * 0.48f) + up * (door_h * 0.62f) + n * 0.085f, Vector3(door_w * 0.36f, door_h * 0.26f, 0.012f), 0.008f, wood.lightened(0.08f));
							}
							add_cbox(door_mb, fb, base + up * (door_h + 0.1f) + n * 0.08f, Vector3(door_w + 0.2f, 0.1f, 0.08f), 0.02f, trim_c);
							add_cbox(kit_stone, fb, base + n * 0.36f + up * 0.075f, Vector3(door_w + 0.25f, 0.075f, 0.3f), 0.02f, stone_c);
							if (fmax > 6 || drng.randf() < 0.4f) {
								add_cbox(door_mb, fb, base + up * (door_h + 0.42f) + n * 0.55f, Vector3(door_w + 0.4f, 0.05f, 0.55f), 0.02f, roof_c);
							}
							parts_doors++;
						}
						// Windows: dark or lit glass just off the wall, inside a repeated frame.
						if (bool(o["windows"]) && windows_count < window_cap) {
							const float ww = fmax <= 2 ? 0.5f : 0.6f, wh = fmax <= 2 ? 0.55f : 0.7f;
							for (int c = 0; c < 4; c++) {
								const int cols = int((len_c[c] - 1.0f) / 3.0f);
								const Vector3 a = f0[c], al = along_c[c], n = n_c[c];
								const Basis fb(al, up, n);
								for (int fl = 0; fl < floors; fl++) {
									for (int col = 0; col < cols; col++) {
										const float at = len_c[c] * (col + 0.5f) / cols;
										if (fl == 0 && c == front && Math::abs(at - door_at) < door_w + ww + 0.35f) {
											continue; // the door is there
										}
										const Vector3 p = a + al * at + n * 0.012f + Vector3(0, fl * floor_h + floor_h * 0.5f, 0);
										const bool lit = drng.randf() < float(double(o["lit_windows"]));
										MeshBuilder &mb = lit ? lit_mb : window_mb;
										mb.quad(p - al * ww - Vector3(0, wh, 0), p + al * ww - Vector3(0, wh, 0), p + al * ww + Vector3(0, wh, 0), p - al * ww + Vector3(0, wh, 0), lit ? Color(1, 0.82f, 0.5f) : Color(0.16f, 0.2f, 0.25f), n);
										block_frames.push_back(Transform3D(fb, p - n * 0.012f));
										windows_count++;
										lit_count += lit ? 1 : 0;
									}
								}
							}
						}
						if (gable) {
							// Ridge along the longer side; eaves stand out 0.45 m down the slope and
							// 0.3 m past the gable walls; the roof is 0.12 m thick.
							const bool long01 = f1[0].distance_to(f1[1]) >= f1[0].distance_to(f1[3]);
							const float short_side = long01 ? f1[0].distance_to(f1[3]) : f1[0].distance_to(f1[1]);
							const Vector3 rise(0, short_side * 0.38f, 0);
							// Sides as (eave start, eave end) with the ridge ends above them.
							Vector3 e[2][2], r0, r1;
							if (long01) {
								r0 = (f1[0] + f1[3]) / 2 + rise;
								r1 = (f1[1] + f1[2]) / 2 + rise;
								e[0][0] = f1[0];
								e[0][1] = f1[1];
								e[1][0] = f1[3];
								e[1][1] = f1[2];
								wall_mb.tri(f1[0], r0, f1[3], wall_c, (f1[0] + f1[3]) / 2 - (f1[1] + f1[2]) / 2);
								wall_mb.tri(f1[1], r1, f1[2], wall_c, (f1[1] + f1[2]) / 2 - (f1[0] + f1[3]) / 2);
							} else {
								r0 = (f1[0] + f1[1]) / 2 + rise;
								r1 = (f1[3] + f1[2]) / 2 + rise;
								e[0][0] = f1[0];
								e[0][1] = f1[3];
								e[1][0] = f1[1];
								e[1][1] = f1[2];
								wall_mb.tri(f1[0], r0, f1[1], wall_c, (f1[0] + f1[1]) / 2 - (f1[2] + f1[3]) / 2);
								wall_mb.tri(f1[3], r1, f1[2], wall_c, (f1[3] + f1[2]) / 2 - (f1[0] + f1[1]) / 2);
							}
							const Vector3 axis = (r1 - r0).normalized();
							const float overhang = 0.45f, gable_out = 0.3f, thick = 0.12f;
							for (int sd = 0; sd < 2; sd++) {
								const Vector3 slope = (e[sd][0] - r0).normalized();
								const Vector3 p0 = e[sd][0] + slope * overhang - axis * gable_out, p1 = e[sd][1] + slope * overhang + axis * gable_out;
								const Vector3 q0 = r0 - axis * gable_out, q1 = r1 + axis * gable_out;
								Vector3 nn = (p1 - p0).cross(q0 - p0).normalized();
								if (nn.y < 0) {
									nn = -nn;
								}
								const Vector3 tk = nn * thick;
								const Color rc = sd == 0 ? roof_c : roof_c.darkened(0.06f);
								kit_roof.quad(p0 + tk, p1 + tk, q1 + tk, q0 + tk, rc, nn);
								kit_trim.quad(p0, p1, q1, q0, trim_c.darkened(0.25f), -nn);
								kit_trim.quad(p0, p1, p1 + tk, p0 + tk, trim_c, slope);
								kit_trim.quad(p0, q0, q0 + tk, p0 + tk, trim_c, -axis);
								kit_trim.quad(p1, q1, q1 + tk, p1 + tk, trim_c, axis);
							}
							const Vector3 side = up.cross(axis).normalized();
							add_cbox(kit_roof, Basis(axis, up, side), (r0 + r1) / 2 + Vector3(0, thick + 0.06f, 0), Vector3(r0.distance_to(r1) * 0.5f + gable_out + 0.04f, 0.07f, 0.13f), 0.03f, roof_c.darkened(0.18f));
							if (drng.randf() < 0.45f) {
								const Vector3 at = r0.lerp(r1, 0.2f + 0.6f * drng.randf()) + (e[0][0] - r0).normalized() * (short_side * 0.18f);
								const float top = r0.y + 0.9f;
								const Basis fb(axis, up, side);
								const Color brick_c = wall.lerp(Color(0.55f, 0.3f, 0.22f), 0.6f);
								add_cbox(kit_brick, fb, Vector3(at.x, (yt + top) * 0.5f, at.z), Vector3(0.34f, (top - yt) * 0.5f, 0.28f), 0.02f, brick_c);
								add_cbox(kit_stone, fb, Vector3(at.x, top + 0.05f, at.z), Vector3(0.42f, 0.06f, 0.36f), 0.02f, stone_c);
								parts_chimneys++;
							}
						} else {
							add_ring(kit_trim, fp, yt - cornice_h, yt, 0.2f, 0.06f, trim_c, true);
							Vector2 po[4], pi[4];
							if (inset_quad(fp, -0.05f, po, false) && inset_quad(fp, 0.22f, pi)) {
								const float ytp = yt + 0.75f;
								auto V = [](const Vector2 &p_p, float p_y) { return Vector3(p_p.x, p_y, p_p.y); };
								for (int c = 0; c < 4; c++) {
									const int j = (c + 1) % 4;
									const Vector3 n = n_c[c];
									wall_mb.quad(V(po[c], yt), V(po[j], yt), V(po[j], ytp), V(po[c], ytp), wall_c, n);
									wall_mb.quad(V(pi[c], yt + 0.02f), V(pi[j], yt + 0.02f), V(pi[j], ytp), V(pi[c], ytp), wall_c.darkened(0.08f), -n);
									kit_stone.quad(V(po[c], ytp), V(po[j], ytp), V(pi[j], ytp), V(pi[c], ytp), stone_c.lightened(0.1f), up);
								}
								kit_flat.quad(V(pi[0], yt + 0.02f), V(pi[1], yt + 0.02f), V(pi[2], yt + 0.02f), V(pi[3], yt + 0.02f), pal.flat_roof, up);
								// Roof equipment: units, a stair house on tall buildings, a water tank in cities.
								const Basis fb(along_c[0], up, n_c[0]);
								const int units = 1 + drng.rand(3);
								for (int k = 0; k < units; k++) {
									const Vector2 rq = bilinear(pi, 0.2f + 0.6f * drng.randf(), 0.2f + 0.6f * drng.randf());
									add_cbox(kit_trim, fb, Vector3(rq.x, yt + 0.42f, rq.y), Vector3(0.6f, 0.4f, 0.45f), 0.04f, Color(0.62f, 0.63f, 0.62f));
									parts_roof_props++;
								}
								if (floors >= 4 && drng.randf() < 0.6f) {
									const Vector2 rq = bilinear(pi, 0.3f + 0.4f * drng.randf(), 0.3f + 0.4f * drng.randf());
									add_cbox(wall_mb, fb, Vector3(rq.x, yt + 1.27f, rq.y), Vector3(1.3f, 1.25f, 1.4f), 0.03f, wall_c);
									add_cbox(kit_stone, fb, Vector3(rq.x, yt + 2.57f, rq.y), Vector3(1.45f, 0.06f, 1.55f), 0.02f, stone_c);
									parts_roof_props++;
								}
								if (fmax > 6 && drng.randf() < 0.4f) {
									const Vector2 rq = bilinear(pi, 0.25f + 0.5f * drng.randf(), 0.25f + 0.5f * drng.randf());
									const Color wood = Color(0.42f, 0.32f, 0.24f);
									for (int lg = 0; lg < 4; lg++) {
										const float ang = Math::TAU * (lg + 0.5f) / 4;
										add_cbox(kit_trim, fb, Vector3(rq.x + Math::cos(ang) * 0.75f, yt + 0.6f, rq.y + Math::sin(ang) * 0.75f), Vector3(0.06f, 0.6f, 0.06f), 0.0f, wood.darkened(0.3f));
									}
									add_prism(kit_trim, Vector3(rq.x, yt + 1.2f, rq.y), 1.05f, 1.05f, 1.9f, 14, wood);
									add_prism(kit_trim, Vector3(rq.x, yt + 3.1f, rq.y), 1.12f, 0.0f, 0.55f, 14, wood.darkened(0.2f), false);
									parts_roof_props++;
								}
							}
						}
					} else {
						for (int c = 0; c < 4; c++) {
							const Vector3 out = (f0[c] + f0[(c + 1) % 4]) / 2 - c3;
							building_mb.quad(f0[c], f0[(c + 1) % 4], f1[(c + 1) % 4], f1[c], wall, out);
							// Windows on every floor.
							if (bool(o["windows"]) && windows_count < window_cap) {
								const Vector3 a = f0[c], b = f0[(c + 1) % 4];
								const float wl = a.distance_to(b);
								const int cols = int((wl - 1.0f) / 3.0f);
								const Vector3 along = (b - a) / MAX(wl, 0.001f);
								const Vector3 n = Vector3(out.x, 0, out.z).normalized();
								const float ww = fmax <= 2 ? 0.5f : 0.6f, wh = fmax <= 2 ? 0.55f : 0.7f;
								for (int fl = 0; fl < floors; fl++) {
									for (int col = 0; col < cols; col++) {
										const Vector3 p = a + along * (wl * (col + 0.5f) / cols) + n * 0.05f + Vector3(0, curb + fl * floor_h + floor_h * 0.5f, 0) - Vector3(0, curb, 0);
										const bool lit = drng.randf() < float(double(o["lit_windows"]));
										MeshBuilder &mb = lit ? lit_mb : window_mb;
										mb.quad(p - along * ww - Vector3(0, wh, 0), p + along * ww - Vector3(0, wh, 0), p + along * ww + Vector3(0, wh, 0), p - along * ww + Vector3(0, wh, 0), lit ? Color(1, 0.82f, 0.5f) : Color(0.16f, 0.2f, 0.25f), n);
										windows_count++;
										lit_count += lit ? 1 : 0;
									}
								}
							}
						}
						const bool gable = gable_roof;
						if (gable) {
							// Ridge along the longer side of the footprint.
							const bool long01 = f1[0].distance_to(f1[1]) >= f1[0].distance_to(f1[3]);
							const float short_side = long01 ? f1[0].distance_to(f1[3]) : f1[0].distance_to(f1[1]);
							const Vector3 rise(0, short_side * 0.38f, 0);
							Vector3 r0, r1;
							if (long01) {
								r0 = (f1[0] + f1[3]) / 2 + rise;
								r1 = (f1[1] + f1[2]) / 2 + rise;
								building_mb.quad(f1[0], f1[1], r1, r0, roof_c, ((f1[0] + f1[1]) / 2 - (f1[2] + f1[3]) / 2) + Vector3(0, 1, 0));
								building_mb.quad(f1[3], f1[2], r1, r0, roof_c.darkened(0.12f), ((f1[2] + f1[3]) / 2 - (f1[0] + f1[1]) / 2) + Vector3(0, 1, 0));
								building_mb.tri(f1[0], r0, f1[3], wall, (f1[0] + f1[3]) / 2 - (f1[1] + f1[2]) / 2);
								building_mb.tri(f1[1], r1, f1[2], wall, (f1[1] + f1[2]) / 2 - (f1[0] + f1[3]) / 2);
							} else {
								r0 = (f1[0] + f1[1]) / 2 + rise;
								r1 = (f1[3] + f1[2]) / 2 + rise;
								building_mb.quad(f1[0], f1[3], r1, r0, roof_c, ((f1[0] + f1[3]) / 2 - (f1[1] + f1[2]) / 2) + Vector3(0, 1, 0));
								building_mb.quad(f1[1], f1[2], r1, r0, roof_c.darkened(0.12f), ((f1[1] + f1[2]) / 2 - (f1[0] + f1[3]) / 2) + Vector3(0, 1, 0));
								building_mb.tri(f1[0], r0, f1[1], wall, (f1[0] + f1[1]) / 2 - (f1[2] + f1[3]) / 2);
								building_mb.tri(f1[3], r1, f1[2], wall, (f1[3] + f1[2]) / 2 - (f1[0] + f1[1]) / 2);
							}
						} else {
							building_mb.quad(f1[0], f1[1], f1[2], f1[3], pal.flat_roof.lerp(roof_c, 0.2f), up);
						}
					}
					if (body) {
						Vector<Vector3> pts;
						for (int c = 0; c < 4; c++) {
							pts.push_back(f0[c]);
							pts.push_back(f1[c]);
						}
						add_convex_shape(body, owner, vformat("Building%d", buildings), pts);
					}
					if (h > tallest) {
						tallest = h;
						tallest_top = Vector3(fc.x, curb + h, fc.y);
					}
					Vector<Vector2> fpv;
					for (int c = 0; c < 4; c++) {
						fpv.push_back(fp[c]);
					}
					footprints.push_back(fpv);
					footprint_h.push_back(h);
					footprint_c.push_back(gable_roof ? roof_c : pal.flat_roof);
					buildings++;
				}
			}
			if (!block_frames.is_empty()) {
				if (frame_mesh.is_null()) {
					const float ww = fmax <= 2 ? 0.5f : 0.6f, wh = fmax <= 2 ? 0.55f : 0.7f;
					frame_mesh = window_frame_mesh(ww, wh, pal.trim, fmax <= 2, pal.roofs[0].darkened(0.1f), trim_material, textured ? 1.0f : 0.0f);
				}
				MultiMeshInstance3D *mmi = add_multimesh(holder, owner, vformat("WindowFrames_%d_%d", bi, bj), frame_mesh, block_frames, true);
				// Frames are fine detail: a block's frames fade out past 160 m.
				mmi->set_visibility_range_end(160.0f);
				mmi->set_visibility_range_end_margin(20.0f);
				mmi->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_SELF);
				frames_total += block_frames.size();
			}
		}
	}
	if (windows_count >= window_cap) {
		hints.push_back(vformat("windows stopped at %d; lower \"floors_max\" or \"size\", or set \"windows\": false", window_cap));
	}

	// Street lamps and trees along the streets, on the sidewalk.
	Vector<Vector3> lamp_positions;
	const float lamp_step = double(o["lamps"]), tree_step = double(o["street_trees"]);
	for (int e = 0; e < edge_a.size(); e++) {
		const Vector2 a = edge_a[e], b = edge_b[e];
		const float len = a.distance_to(b);
		const Vector2 dir = (b - a) / MAX(len, 0.001f);
		const Vector2 side = dir.orthogonal();
		const float off = road_w * 0.5f + MIN(0.7f, walk * 0.5f);
		const bool on_edge_a = Math::abs(a.x) > half - 0.01f || Math::abs(a.y) > half - 0.01f;
		const bool on_edge_b = Math::abs(b.x) > half - 0.01f || Math::abs(b.y) > half - 0.01f;
		const bool border = on_edge_a && on_edge_b; // streets on the city's edge have houses on one side only
		if (lamp_step > 0) {
			const int count = int(len / lamp_step);
			for (int l = 0; l < count; l++) {
				const float t = (l + 0.5f) / count;
				float sgn = (l % 2) ? 1.0f : -1.0f;
				if (border) {
					sgn = side.dot(-a.normalized()) > 0 ? 1.0f : -1.0f; // the inner side
				}
				const Vector2 p = a.lerp(b, t) + side * off * sgn;
				lamp_positions.push_back(Vector3(p.x, curb, p.y));
			}
		}
		if (tree_step > 0 && walk >= 1.4f) {
			const int count = int(len / tree_step);
			for (int l = 0; l < count; l++) {
				const float t = (l + 0.25f) / count;
				const float sgn = border ? (side.dot(-a.normalized()) > 0 ? 1.0f : -1.0f) : ((l % 2) ? -1.0f : 1.0f);
				const Vector2 p = a.lerp(b, t) + side * (road_w * 0.5f + walk * 0.55f) * sgn;
				street_trees.push_back(Transform3D(Basis(up, hash01(e, l, seed) * Math::TAU).scaled(Vector3(0.7f, 0.7f, 0.7f)), Vector3(p.x, curb, p.y)));
			}
		}
	}
	Node3D *lights = nullptr;
	const int max_lights = int64_t(o["max_lights"]);
	const int light_every = lamp_positions.size() > max_lights && max_lights > 0 ? int(Math::ceil(double(lamp_positions.size()) / max_lights)) : 1;
	int light_count = 0;
	if (!lamp_positions.is_empty()) {
		lights = memnew(Node3D);
		lights->set_name("Lights");
		holder->add_child(lights, true);
		lights->set_owner(owner);
	}
	MeshBuilder bulb_mb;
	for (int l = 0; l < lamp_positions.size(); l++) {
		const Vector3 p = lamp_positions[l];
		add_prism(post_mb, p, 0.09f, 0.07f, 4.2f, 6, Color::html("#2f3033"));
		add_prism(bulb_mb, p + Vector3(0, 4.05f, 0), 0.22f, 0.22f, 0.3f, 6, Color(1, 0.85f, 0.55f));
		if (max_lights > 0 && l % light_every == 0 && light_count < max_lights) {
			OmniLight3D *light = memnew(OmniLight3D);
			UZULook::configure_lamp(light, UZULook::QUALITY_STANDARD);
			light->set_name(vformat("Lamp%d", light_count));
			light->set_position(p + Vector3(0, 3.9f, 0));
			lights->add_child(light, true);
			light->set_owner(owner);
			light_count++;
		}
	}
	if (lamp_positions.size() > light_count && lamp_step > 0) {
		hints.push_back(vformat("%d lamps, %d of them emit light (max_lights); the rest only glow", lamp_positions.size(), light_count));
	}

	// Benches (B-1507): along the sidewalks, facing the street, and around the plaza's fountain.
	MeshBuilder bench_mb;
	Vector<Vector3> bench_pos;
	Vector<float> bench_yaw;
	const float bench_step = double(o["benches"]);
	if (bench_step > 0 && walk >= 1.4f) {
		for (int e = 0; e < edge_a.size(); e++) {
			const Vector2 a = edge_a[e], b = edge_b[e];
			const float len = a.distance_to(b);
			const Vector2 dir = (b - a) / MAX(len, 0.001f);
			const Vector2 side = dir.orthogonal();
			const bool border = (Math::abs(a.x) > half - 0.01f || Math::abs(a.y) > half - 0.01f) && (Math::abs(b.x) > half - 0.01f || Math::abs(b.y) > half - 0.01f);
			const int count = int(len / bench_step);
			for (int l = 0; l < count; l++) {
				const float t = (l + 0.62f) / count;
				const float sgn = border ? (side.dot(-a.normalized()) > 0 ? 1.0f : -1.0f) : ((l % 2) ? 1.0f : -1.0f);
				const Vector2 p = a.lerp(b, t) + side * (road_w * 0.5f + walk * 0.8f) * sgn;
				const Vector2 face = -side * sgn; // Toward the street.
				bench_pos.push_back(Vector3(p.x, curb, p.y));
				bench_yaw.push_back(Math::atan2(face.x, face.y));
			}
		}
	}
	if (plaza && plaza_center != Vector3()) {
		for (int t = 0; t < 4; t++) {
			const float ang = Math::PI * 0.25f + t * Math::PI * 0.5f;
			const Vector3 p = plaza_center + Vector3(Math::cos(ang), 0, Math::sin(ang)) * 6.5f;
			bench_pos.push_back(p);
			bench_yaw.push_back(Math::atan2(plaza_center.x - p.x, plaza_center.z - p.z)); // Facing the fountain.
		}
	}
	for (int k = 0; k < bench_pos.size(); k++) {
		const Vector3 p = bench_pos[k];
		const float yaw = bench_yaw[k];
		const Basis b(Vector3(0, 1, 0), yaw);
		const Color wood = Color::html("#7a5a3a"), iron = Color::html("#2f3033");
		add_box(bench_mb, p + Vector3(0, 0.45f, 0), Vector3(0.8f, 0.035f, 0.22f), yaw, wood);
		add_box(bench_mb, p - b.get_column(2) * 0.2f + Vector3(0, 0.72f, 0), Vector3(0.8f, 0.2f, 0.025f), yaw, wood);
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			add_box(bench_mb, p + b.get_column(0) * (0.66f * sgn) + Vector3(0, 0.22f, 0), Vector3(0.035f, 0.22f, 0.2f), yaw, iron);
		}
	}
	const int bench_triangles = bench_mb.triangles();
	if (bench_mb.triangles() > 0) {
		add_mesh(holder, owner, "Benches", bench_mb.commit(vertex_color_material(0.8f)));
	}
	// Benches are furniture a character should not walk through (B-1508):
	// one convex box each, the same footprint the navigation bake cuts out.
	if (body) {
		for (int k = 0; k < bench_pos.size(); k++) {
			const Vector3 p = bench_pos[k];
			const Basis rot(Vector3(0, 1, 0), bench_yaw[k]);
			Vector<Vector3> pts;
			for (int sx = -1; sx <= 1; sx += 2) {
				for (int sz = -1; sz <= 1; sz += 2) {
					const Vector3 off = rot.xform(Vector3(0.82f * sx, 0, 0.26f * sz));
					pts.push_back(p + off);
					pts.push_back(p + off + Vector3(0, 0.95f, 0));
				}
			}
			add_convex_shape(body, owner, vformat("Bench%d", k), pts);
		}
	}

	if (bool(o["ground"])) {
		add_mesh(holder, owner, "Ground", ground_mb.commit(vertex_color_material(0.95f)), false);
	}
	add_mesh(holder, owner, "Roads", road_mb.commit(vertex_color_material(0.8f)), false);
	add_mesh(holder, owner, "Blocks", block_mb.commit(vertex_color_material(0.85f)));
	if (detailed) {
		add_mesh(holder, owner, "Buildings", kit_wall.commit(kit_material(TEX_PLASTER, 0.88f)));
		add_mesh(holder, owner, "BuildingsBrick", kit_brick.commit(kit_material(TEX_BRICK, 0.85f)));
		add_mesh(holder, owner, "BuildingsStone", kit_stone.commit(kit_material(TEX_STONE, 0.9f)));
		add_mesh(holder, owner, "BuildingsTrim", kit_trim.commit(trim_material));
		add_mesh(holder, owner, "Roofs", kit_roof.commit(kit_material(TEX_TILE, 0.7f)));
		add_mesh(holder, owner, "RoofsFlat", kit_flat.commit(kit_material(TEX_CONCRETE, 0.9f)));
	} else {
		add_mesh(holder, owner, "Buildings", building_mb.commit(vertex_color_material(0.88f)));
	}
	add_mesh(holder, owner, "Windows", window_mb.commit(vertex_color_material(0.15f)), false);
	add_mesh(holder, owner, "WindowsLit", lit_mb.commit(glow_material(Color(1, 0.82f, 0.5f), 1.6f)), false);
	add_mesh(holder, owner, "LampPosts", post_mb.commit(vertex_color_material(0.5f)));
	add_mesh(holder, owner, "LampBulbs", bulb_mb.commit(glow_material(Color(1, 0.85f, 0.55f), 3.0f)), false);
	add_mesh(holder, owner, "Landmark", landmark_mb.commit(vertex_color_material(0.7f)));
	add_multimesh(holder, owner, "StreetTrees", broadleaf_tree_mesh(), street_trees, true);

	// Places.
	const Vector3 origin = holder->get_position();
	Dictionary places;
	int center_node = 0;
	for (int i = 1; i < nodes.size(); i++) {
		center_node = nodes[i].length() < nodes[center_node].length() ? i : center_node;
	}
	places["spawn"] = place(origin + Vector3(nodes[center_node].x, ry, nodes[center_node].y), "a crossing near the center, on the street");
	if (plaza) {
		places["plaza"] = place(origin + plaza_center + Vector3(0, 0, 4.5f), "the open square in the middle, next to the fountain");
	}
	if (buildings > 0) {
		places["tallest_roof"] = place(origin + tallest_top, vformat("the top of the tallest building (%d m)", int(tallest)));
	}
	const int mid = m / 2;
	places["gate_north"] = place(origin + Vector3(N(mid, 0).x, ry, N(mid, 0).y), "where a street leaves the city on the -z side");
	places["gate_south"] = place(origin + Vector3(N(mid, m).x, ry, N(mid, m).y), "where a street leaves the city on the +z side");
	places["gate_west"] = place(origin + Vector3(N(0, mid).x, ry, N(0, mid).y), "where a street leaves the city on the -x side");
	places["gate_east"] = place(origin + Vector3(N(m, mid).x, ry, N(m, mid).y), "where a street leaves the city on the +x side");

	// Overview.
	const int ow = 256;
	Ref<Image> overview = Image::create_empty(ow, ow, false, Image::FORMAT_RGB8);
	overview->fill(pal.ground);
	for (int e = 0; e < edge_a.size(); e++) {
		const Vector2 a = edge_a[e], b = edge_b[e];
		const Vector2 side = (b - a).normalized().orthogonal() * (road_w * 0.5f);
		const Vector2 q[4] = { a + side, b + side, b - side, a - side };
		fill_convex(overview, q, 4, half, size, pal.road);
	}
	for (const Vector<Vector2> &s : slabs) {
		fill_convex(overview, s.ptr(), 4, half, size, pal.sidewalk);
	}
	for (int i = 0; i < footprints.size(); i++) {
		const float t = fmax > fmin ? CLAMP(footprint_h[i] / (fmax * floor_h), 0.0f, 1.0f) : 0.5f;
		fill_convex(overview, footprints[i].ptr(), 4, half, size, footprint_c[i].darkened(0.35f * (1.0f - t)).lightened(0.25f * t));
	}
	auto to_px = [&](const Vector3 &p_local) { return Vector2i(int((p_local.x + half) / size * ow), int((p_local.z + half) / size * ow)); };
	for (const Transform3D &t : street_trees) {
		const Vector2i p = to_px(t.origin);
		mark(overview, p.x, p.y, Color(0.15f, 0.4f, 0.15f), 1);
	}
	for (const Vector3 &p : lamp_positions) {
		const Vector2i q = to_px(p);
		mark(overview, q.x, q.y, Color(1, 0.85f, 0.3f), 0);
	}
	if (plaza) {
		const Vector2i q = to_px(plaza_center);
		mark(overview, q.x, q.y, Color(0.3f, 0.7f, 1), 3);
	}
	const Vector2i sp = to_px(Vector3(nodes[center_node].x, 0, nodes[center_node].y));
	mark(overview, sp.x, sp.y, Color(1, 0.15f, 0.15f), 3);

	Dictionary counts;
	counts["blocks"] = m * m;
	counts["buildings"] = buildings;
	counts["empty_lots"] = empty_lots;
	// Navigation (B-1507): a NavigationMesh of the ground and the sidewalk tops, with the
	// buildings, trees, lamps, benches and the fountain cut out; then a check that the
	// places (spawn, plaza, gates) are on one walkable piece.
	Dictionary navigation;
	if (bool(o["navigation"]) && NavigationServer3D::get_singleton()) {
		const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
		PackedVector3Array faces;
		auto up_tri = [&](const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
			// Godot's front side is where the corners go clockwise: make it face up.
			const bool flip = (p_c - p_a).cross(p_b - p_a).y < 0;
			faces.push_back(p_a);
			faces.push_back(flip ? p_c : p_b);
			faces.push_back(flip ? p_b : p_c);
		};
		const float gh = half + margin;
		up_tri(Vector3(-gh, 0, -gh), Vector3(gh, 0, -gh), Vector3(gh, 0, gh));
		up_tri(Vector3(-gh, 0, -gh), Vector3(gh, 0, gh), Vector3(-gh, 0, gh));
		for (const Vector<Vector2> &sl : slabs) {
			up_tri(Vector3(sl[0].x, curb, sl[0].y), Vector3(sl[1].x, curb, sl[1].y), Vector3(sl[2].x, curb, sl[2].y));
			up_tri(Vector3(sl[0].x, curb, sl[0].y), Vector3(sl[2].x, curb, sl[2].y), Vector3(sl[3].x, curb, sl[3].y));
		}
		Ref<NavigationMeshSourceGeometryData3D> src;
		src.instantiate();
		src->add_faces(faces, Transform3D());
		auto obstruct = [&](const Vector3 &p_center, float p_half_x, float p_half_z, float p_yaw, float p_height) {
			const Basis b(Vector3(0, 1, 0), p_yaw);
			const Vector3 ax = b.get_column(0) * p_half_x, az = b.get_column(2) * p_half_z;
			Vector<Vector3> pts;
			pts.push_back(p_center - ax - az);
			pts.push_back(p_center + ax - az);
			pts.push_back(p_center + ax + az);
			pts.push_back(p_center - ax + az);
			src->add_projected_obstruction(pts, p_center.y - 0.2f, p_height, false); // Not carved: widened by the agent radius like geometry.
		};
		for (int k = 0; k < footprints.size(); k++) {
			Vector<Vector3> pts;
			for (const Vector2 &v : footprints[k]) {
				pts.push_back(Vector3(v.x, curb, v.y));
			}
			src->add_projected_obstruction(pts, curb - 0.2f, footprint_h[k] + 1.0f, false); // Not carved: widened by the agent radius like geometry.
		}
		for (const Transform3D &t : street_trees) {
			obstruct(t.origin, 0.3f, 0.3f, 0, 3.0f);
		}
		for (const Vector3 &l : lamp_positions) {
			obstruct(l, 0.15f, 0.15f, 0, 4.5f);
		}
		for (int k = 0; k < bench_pos.size(); k++) {
			obstruct(bench_pos[k], 0.82f, 0.26f, bench_yaw[k], 1.0f);
		}
		if (plaza && plaza_center != Vector3()) {
			Vector<Vector3> ring;
			for (int t = 0; t < 12; t++) {
				ring.push_back(plaza_center + Vector3(Math::cos(t * Math::TAU / 12), 0, Math::sin(t * Math::TAU / 12)) * 3.3f);
			}
			src->add_projected_obstruction(ring, curb - 0.2f, 4.5f, false); // Not carved: widened by the agent radius like geometry.
		}
		Ref<NavigationMesh> nm;
		nm.instantiate();
		nm->set_cell_size(0.25f);
		nm->set_cell_height(0.25f);
		nm->set_agent_height(1.8f);
		nm->set_agent_radius(0.5f);
		nm->set_agent_max_climb(0.3f);
		nm->set_agent_max_slope(45);
		nm->set_edge_max_error(0.5f); // The default 1.3 cells lets simplified edges cut building corners by ~0.3 m.
		NavigationServer3D::get_singleton()->bake_from_source_geometry_data(nm, src);
		NavigationRegion3D *region = memnew(NavigationRegion3D);
		region->set_name("Navigation");
		region->set_navigation_mesh(nm);
		holder->add_child(region, true);
		region->set_owner(owner);
		// Which walkable piece is each polygon on (polygons sharing an edge are joined)?
		const Vector<Vector3> nv = nm->get_vertices();
		const int pc = nm->get_polygon_count();
		Vector<int> comp;
		comp.resize(pc);
		for (int i = 0; i < pc; i++) {
			comp.write[i] = i;
		}
		auto find = [&](int p_a) {
			while (comp[p_a] != p_a) {
				p_a = comp[p_a];
			}
			return p_a;
		};
		HashMap<uint64_t, int> edge_owner;
		for (int i = 0; i < pc; i++) {
			const Vector<int> poly = nm->get_polygon(i);
			for (int e = 0; e < poly.size(); e++) {
				const uint32_t va = poly[e], vb = poly[(e + 1) % poly.size()];
				const uint64_t key = (uint64_t(MIN(va, vb)) << 32) | MAX(va, vb);
				if (const int *other = edge_owner.getptr(key)) {
					comp.write[find(i)] = find(*other);
				} else {
					edge_owner[key] = i;
				}
			}
		}
		HashSet<int> islands;
		for (int i = 0; i < pc; i++) {
			islands.insert(find(i));
		}
		// The polygon under (or nearest to) a place, in the holder's space.
		auto poly_at = [&](const Vector3 &p_local) {
			int best = -1;
			float best_d = 1e20f;
			for (int i = 0; i < pc; i++) {
				const Vector<int> poly = nm->get_polygon(i);
				Vector3 c;
				bool inside = true;
				for (int e = 0; e < poly.size(); e++) {
					const Vector3 a = nv[poly[e]], b = nv[poly[(e + 1) % poly.size()]];
					c += a;
					const float cross = (b.x - a.x) * (p_local.z - a.z) - (b.z - a.z) * (p_local.x - a.x);
					inside = inside && cross >= -1e-4f;
				}
				c /= MAX(1, poly.size());
				const float d = inside ? 0.0f : Vector2(c.x - p_local.x, c.z - p_local.z).length();
				if (d < best_d) {
					best_d = d;
					best = i;
				}
			}
			return best_d < 4.0f ? best : -1;
		};
		Array unreached;
		int piece = -2;
		bool together = true;
		for (const char *name : { "spawn", "plaza", "gate_north", "gate_south", "gate_west", "gate_east" }) {
			if (!places.has(name)) {
				continue;
			}
			const Array pp = Dictionary(places[name])["position"];
			const int pid = poly_at(Vector3(pp[0], pp[1], pp[2]) - origin);
			if (pid < 0) {
				unreached.push_back(name);
				together = false;
				continue;
			}
			const int c = find(pid);
			if (piece == -2) {
				piece = c;
			} else if (c != piece) {
				unreached.push_back(name);
				together = false;
			}
		}
		navigation["polygons"] = pc;
		navigation["vertices"] = nv.size();
		navigation["islands"] = islands.size();
		navigation["places_connected"] = together && pc > 0;
		navigation["unreached"] = unreached;
		navigation["bake_ms"] = Math::snapped((OS::get_singleton()->get_ticks_usec() - t0) / 1000.0, 0.1);
		if (!together) {
			hints.push_back("some places are not on the same walkable area of the navigation mesh; see navigation.unreached");
		}
	}
	holder->set_meta("uzu_places", places); // For self-verifying play on the saved scene (map walk).
	counts["benches"] = bench_pos.size();
	if (detailed) {
		Dictionary parts;
		parts["plinths"] = parts_plinths;
		parts["string_courses"] = parts_bands;
		parts["pilasters"] = parts_pilasters;
		parts["doors"] = parts_doors;
		parts["chimneys"] = parts_chimneys;
		parts["roof_props"] = parts_roof_props;
		parts["window_frames"] = frames_total;
		counts["parts"] = parts;
	}
	counts["windows"] = windows_count;
	counts["windows_lit"] = lit_count;
	counts["lamps"] = lamp_positions.size();
	counts["lights"] = light_count;
	counts["street_trees"] = street_trees.size();
	Dictionary r;
	r["ok"] = true;
	r["kind"] = "city";
	r["node"] = String(p_root->get_path_to(holder));
	r["preset"] = o["preset"];
	r["seed"] = o["seed"];
	r["size"] = size;
	r["streets_m"] = Math::snapped(double(road_length), 0.1);
	r["tallest_m"] = Math::snapped(double(tallest), 0.1);
	r["triangles"] = ground_mb.triangles() + road_mb.triangles() + block_mb.triangles() + building_mb.triangles() + window_mb.triangles() + lit_mb.triangles() + post_mb.triangles() + bulb_mb.triangles() + landmark_mb.triangles() + bench_triangles + kit_wall.triangles() + kit_brick.triangles() + kit_stone.triangles() + kit_trim.triangles() + kit_roof.triangles() + kit_flat.triangles() + (frame_mesh.is_valid() ? frames_total * frame_mesh->get_faces().size() : 0);
	r["counts"] = counts;
	Dictionary bd;
	bd["detail"] = o["detail"];
	bd["textures"] = textured ? Variant(String("plaster,brick,roof_tiles,stone,concrete").split(",")) : Variant(Array());
	bd["about"] = detailed ? "buildings made of parts; window frames are one repeated part per block (MultiMesh, out past 160 m)" : "plain boxes with flat windows";
	r["buildings_detail"] = bd;
	r["places"] = places;
	if (!navigation.is_empty()) {
		r["navigation"] = navigation;
	}
	r["hints"] = hints;
	r["overview"] = overview;
	r["overview_about"] = "256x256 top-down: x to the right, +z down; red spawn, light blue plaza, yellow dots lamps, green dots trees; brighter roofs are taller";
	Dictionary echo = o.duplicate();
	echo["position"] = vec_array(o["position"]);
	r["options"] = echo;
	return r;
}

void UZUWorldGen::_bind_methods() {
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("get_options", "kind"), &UZUWorldGen::get_options);
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("get_presets", "kind"), &UZUWorldGen::get_presets);
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("resolve", "kind", "spec"), &UZUWorldGen::resolve);
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("generate_nature", "root", "spec"), &UZUWorldGen::generate_nature, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("generate_city", "root", "spec"), &UZUWorldGen::generate_city, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUWorldGen", D_METHOD("height_at", "nature", "x", "z"), &UZUWorldGen::height_at);
}
