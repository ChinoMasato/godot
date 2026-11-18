/**************************************************************************/
/*  uzu_scatter.h                                                         */
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

#include "core/math/transform_3d.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "scene/resources/mesh.h"

class MeshInstance3D;
class MultiMeshInstance3D;
class Node;

// U.ZU scatter (BACKLOG B-1203, Phase 12 richness): small props (stones, tufts, flowers,
// debris) spread over a surface. Points are drawn evenly by area over the surface's
// triangles, with a fixed seed so the same options always give the same layout, and
// filtered by slope, exclusion zones and a minimum spacing. The props are drawn as one
// MultiMesh, so thousands cost about one draw call.
class UZUScatter : public Object {
	GDCLASS(UZUScatter, Object);

protected:
	static void _bind_methods();

public:
	// Options and defaults (see get_defaults()): density (props per square meter), seed,
	// scale_min, scale_max, random_yaw (turn each prop around its up axis), align (0..1:
	// how much a prop leans with the surface), max_slope (degrees; steeper ground gets
	// none), min_distance (meters between props), sink (meters pushed into the
	// ground), max_count, exclude (an Array of {"center": Vector3, "radius": float},
	// circles on the ground plane, or {"aabb": AABB}).
	static Dictionary get_defaults();
	// The transforms of the props on p_surface (triangles in p_surface_transform's
	// space). r_stats, when given, gets {"count", "area", "target", "rejected_slope",
	// "rejected_excluded", "rejected_spacing"}.
	static Vector<Transform3D> compute(const Ref<Mesh> &p_surface, const Transform3D &p_surface_transform, const Dictionary &p_options, Dictionary *r_stats = nullptr);
	static TypedArray<Transform3D> compute_transforms(const Ref<Mesh> &p_surface, const Transform3D &p_surface_transform, const Dictionary &p_options);
	// Scatters p_mesh over p_surface and adds the props as a MultiMeshInstance3D named
	// p_name under p_root (owned by it, so it is saved with the scene). The surface's
	// transform is taken relative to p_root. Returns the stats with "node" and "bounds"
	// (an AABB around the props' positions, in p_root's space), or
	// {"ok": false, "error"}.
	static Dictionary scatter(Node *p_root, MeshInstance3D *p_surface, const Ref<Mesh> &p_mesh, const Dictionary &p_options = Dictionary(), const String &p_name = "Scatter");
};
