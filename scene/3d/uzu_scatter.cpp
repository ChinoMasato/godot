/**************************************************************************/
/*  uzu_scatter.cpp                                                       */
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

#include "uzu_scatter.h"

#include "core/math/random_pcg.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/multimesh.h"

namespace {

Dictionary defaults() {
	Dictionary d;
	d["density"] = 1.0;
	d["seed"] = 1;
	d["scale_min"] = 0.8;
	d["scale_max"] = 1.2;
	d["random_yaw"] = true;
	d["align"] = 0.0;
	d["max_slope"] = 90.0;
	d["min_distance"] = 0.0;
	d["sink"] = 0.0;
	d["max_count"] = 100000;
	d["exclude"] = Array();
	return d;
}

bool excluded(const Vector3 &p_point, const Array &p_zones) {
	for (const Variant &zone_v : p_zones) {
		const Dictionary zone = zone_v;
		if (zone.has("aabb")) {
			if (AABB(zone["aabb"]).has_point(p_point)) {
				return true;
			}
		} else if (zone.has("center")) {
			const Vector3 c = zone["center"];
			const float r = zone.get("radius", 1.0);
			if (Vector2(p_point.x - c.x, p_point.z - c.z).length_squared() <= r * r) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

Dictionary UZUScatter::get_defaults() {
	return defaults();
}

Vector<Transform3D> UZUScatter::compute(const Ref<Mesh> &p_surface, const Transform3D &p_surface_transform, const Dictionary &p_options, Dictionary *r_stats) {
	Vector<Transform3D> result;
	ERR_FAIL_COND_V(p_surface.is_null(), result);
	Dictionary o = defaults();
	for (const Variant &key : p_options.keys()) {
		ERR_FAIL_COND_V_MSG(!o.has(key), result, vformat("UZUScatter: unknown option \"%s\" (see get_defaults).", String(key)));
		o[key] = p_options[key];
	}
	const float density = MAX(0.0f, float(o["density"]));
	const float scale_min = MAX(0.0f, float(o["scale_min"]));
	const float scale_max = MAX(scale_min, float(o["scale_max"]));
	const bool random_yaw = o["random_yaw"];
	const float align = CLAMP(float(o["align"]), 0.0f, 1.0f);
	const float min_up = Math::cos(Math::deg_to_rad(CLAMP(float(o["max_slope"]), 0.0f, 90.0f)));
	const float min_distance = MAX(0.0f, float(o["min_distance"]));
	const float sink = o["sink"];
	const int max_count = MAX(0, int(o["max_count"]));
	const Array zones = o["exclude"];

	// The triangles in the target space, and their cumulative areas.
	const Vector<Face3> faces = p_surface->get_faces();
	Vector<Face3> tris;
	Vector<double> cumulative;
	double area = 0.0;
	for (const Face3 &f : faces) {
		const Face3 t(p_surface_transform.xform(f.vertex[0]), p_surface_transform.xform(f.vertex[1]), p_surface_transform.xform(f.vertex[2]));
		const double a = t.get_area();
		if (a <= 1e-9) {
			continue;
		}
		area += a;
		tris.push_back(t);
		cumulative.push_back(area);
	}
	const int target = MIN(max_count, int(Math::round(area * density)));
	int rejected_slope = 0;
	int rejected_excluded = 0;
	int rejected_spacing = 0;

	if (target > 0 && !tris.is_empty()) {
		const uint64_t seed = uint64_t(int64_t(o["seed"]));
		RandomPCG rng(seed * 2654435761u + 1u);
		// A grid of cells min_distance wide, for the spacing check.
		HashMap<Vector3i, LocalVector<Vector3>> grid;
		const float cell = MAX(min_distance, 1e-4f);
		auto cell_of = [&](const Vector3 &p) { return Vector3i(Math::floor(p.x / cell), Math::floor(p.y / cell), Math::floor(p.z / cell)); };
		// Each point is drawn the same way whatever happens to the others, so a seed always
		// gives the same layout; spacing may reject some, so a few more attempts are made.
		const int attempts = min_distance > 0.0f ? target * 4 : target;
		for (int i = 0; i < attempts && result.size() < target; i++) {
			const double pick = rng.randf() * area;
			int lo = 0;
			int hi = cumulative.size() - 1;
			while (lo < hi) {
				const int mid = (lo + hi) / 2;
				if (cumulative[mid] < pick) {
					lo = mid + 1;
				} else {
					hi = mid;
				}
			}
			const Face3 &t = tris[lo];
			float u = rng.randf();
			float v = rng.randf();
			if (u + v > 1.0f) {
				u = 1.0f - u;
				v = 1.0f - v;
			}
			const Vector3 point = t.vertex[0] + (t.vertex[1] - t.vertex[0]) * u + (t.vertex[2] - t.vertex[0]) * v;
			const float yaw = rng.randf() * Math::TAU;
			const float scale = scale_min + rng.randf() * (scale_max - scale_min);
			Vector3 normal = t.get_plane().normal;
			if (normal.y < 0.0f) {
				normal = -normal; // Either winding: the side facing up.
			}
			if (normal.y < min_up - 1e-5f) {
				rejected_slope++;
				continue;
			}
			if (excluded(point, zones)) {
				rejected_excluded++;
				continue;
			}
			if (min_distance > 0.0f) {
				const Vector3i c = cell_of(point);
				bool too_close = false;
				for (int dz = -1; dz <= 1 && !too_close; dz++) {
					for (int dy = -1; dy <= 1 && !too_close; dy++) {
						for (int dx = -1; dx <= 1 && !too_close; dx++) {
							const LocalVector<Vector3> *cell_points = grid.getptr(c + Vector3i(dx, dy, dz));
							if (cell_points) {
								for (const Vector3 &q : *cell_points) {
									if (q.distance_squared_to(point) < min_distance * min_distance) {
										too_close = true;
										break;
									}
								}
							}
						}
					}
				}
				if (too_close) {
					rejected_spacing++;
					continue;
				}
				grid[c].push_back(point);
			}
			// Up leans from straight up toward the surface normal by align.
			const Vector3 up = Vector3(0, 1, 0).lerp(normal, align).normalized();
			// Forward is world +Z laid onto the plane across up (or +X when up is along Z).
			const Vector3 ref = Math::abs(up.z) < 0.99f ? Vector3(0, 0, 1) : Vector3(1, 0, 0);
			const Vector3 back = (ref - up * up.dot(ref)).normalized();
			Basis basis(up.cross(back), up, back);
			if (random_yaw) {
				basis = Basis(up, yaw) * basis;
			}
			basis.scale(Vector3(scale, scale, scale));
			result.push_back(Transform3D(basis, point - up * sink));
		}
	}
	if (r_stats) {
		(*r_stats)["count"] = result.size();
		(*r_stats)["area"] = area;
		(*r_stats)["target"] = target;
		(*r_stats)["rejected_slope"] = rejected_slope;
		(*r_stats)["rejected_excluded"] = rejected_excluded;
		(*r_stats)["rejected_spacing"] = rejected_spacing;
	}
	return result;
}

TypedArray<Transform3D> UZUScatter::compute_transforms(const Ref<Mesh> &p_surface, const Transform3D &p_surface_transform, const Dictionary &p_options) {
	TypedArray<Transform3D> out;
	for (const Transform3D &t : compute(p_surface, p_surface_transform, p_options)) {
		out.push_back(t);
	}
	return out;
}

Dictionary UZUScatter::scatter(Node *p_root, MeshInstance3D *p_surface, const Ref<Mesh> &p_mesh, const Dictionary &p_options, const String &p_name) {
	Dictionary result;
	result["ok"] = false;
	if (p_root == nullptr || p_surface == nullptr || p_surface->get_mesh().is_null() || p_mesh.is_null()) {
		result["error"] = "scatter needs a root, a surface with a mesh, and a mesh to scatter";
		return result;
	}
	// The surface's transform relative to the root (inside or outside the tree).
	Transform3D to_root;
	for (Node *n = p_surface; n && n != p_root; n = n->get_parent()) {
		if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
			to_root = n3->get_transform() * to_root;
		}
	}
	Dictionary stats;
	const Vector<Transform3D> transforms = compute(p_surface->get_mesh(), to_root, p_options, &stats);
	if (stats.is_empty()) {
		result["error"] = "invalid options (see get_defaults)";
		return result;
	}
	Ref<MultiMesh> multimesh;
	multimesh.instantiate();
	multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	multimesh->set_mesh(p_mesh);
	multimesh->set_instance_count(transforms.size());
	// All transforms in one buffer (12 floats each: the basis rows with the origin), which
	// is also what is saved with the scene; per-instance setters do not reach the saved
	// data when the engine runs headless.
	Vector<float> buffer;
	buffer.resize(transforms.size() * 12);
	float *w = buffer.ptrw();
	for (int i = 0; i < transforms.size(); i++) {
		const Transform3D &t = transforms[i];
		for (int row = 0; row < 3; row++) {
			w[i * 12 + row * 4 + 0] = t.basis.rows[row].x;
			w[i * 12 + row * 4 + 1] = t.basis.rows[row].y;
			w[i * 12 + row * 4 + 2] = t.basis.rows[row].z;
			w[i * 12 + row * 4 + 3] = t.origin[row];
		}
	}
	multimesh->set_buffer(buffer);
	MultiMeshInstance3D *instance = memnew(MultiMeshInstance3D);
	instance->set_name(p_name.is_empty() ? String("Scatter") : p_name);
	instance->set_multimesh(multimesh);
	p_root->add_child(instance, true);
	instance->set_owner(p_root->get_owner() ? p_root->get_owner() : p_root);
	// Where the props ended up (their positions, in the root's space).
	AABB bounds;
	for (int i = 0; i < transforms.size(); i++) {
		if (i == 0) {
			bounds = AABB(transforms[i].origin, Vector3());
		} else {
			bounds.expand_to(transforms[i].origin);
		}
	}
	stats["ok"] = true;
	stats["node"] = String(p_root->get_path_to(instance));
	stats["bounds"] = bounds;
	return stats;
}

void UZUScatter::_bind_methods() {
	ClassDB::bind_static_method("UZUScatter", D_METHOD("get_defaults"), &UZUScatter::get_defaults);
	ClassDB::bind_static_method("UZUScatter", D_METHOD("compute_transforms", "surface", "surface_transform", "options"), &UZUScatter::compute_transforms, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUScatter", D_METHOD("scatter", "root", "surface", "mesh", "options", "name"), &UZUScatter::scatter, DEFVAL(Dictionary()), DEFVAL("Scatter"));
}
