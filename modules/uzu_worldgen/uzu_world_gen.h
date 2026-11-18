/**************************************************************************/
/*  uzu_world_gen.h                                                       */
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

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"

class Node;
class Node3D;

// U.ZU world generation (owner request 2026-11-19): a natural map (terrain, water, trees,
// rocks, grass) or a city map (roads, blocks, buildings, windows, lamps, street trees)
// from one short spec, built for an AI to ask for.
//
// - Everything in the spec is optional: a preset ("hills", "island", "town", ...) gives
//   the rest, and any key overrides it. The same spec always gives the same map (seed).
// - A wrong spec adds nothing and says exactly what to change: the unknown key with the
//   closest valid one, the expected type, or the allowed range.
// - The result says what was made in numbers, and names places to use next ("spawn",
//   "peak", "lake", "plaza", "gate_north", ...) with their positions, plus a small
//   top-down overview image (x to the right, +z down) to check the layout at a glance.
// - get_options()/get_presets() list every key with its default, range and meaning.
class UZUWorldGen : public Object {
	GDCLASS(UZUWorldGen, Object);

protected:
	static void _bind_methods();

public:
	// p_kind is "nature" or "city".
	static Dictionary get_options(const String &p_kind);
	static Dictionary get_presets(const String &p_kind);
	// The full option set for a spec (defaults < preset < spec), or {"ok": false, "error",
	// "key", "hint"}. Nothing is built.
	static Dictionary resolve(const String &p_kind, const Dictionary &p_spec);

	// Builds the map under p_root (new nodes are owned by p_root's owner, or p_root, so
	// they are saved with the scene). Returns {"ok", "kind", "node", "options", "counts",
	// "places", "overview" (Image), ...} or {"ok": false, "error", ...}.
	static Dictionary generate_nature(Node *p_root, const Dictionary &p_spec = Dictionary());
	static Dictionary generate_city(Node *p_root, const Dictionary &p_spec = Dictionary());

	// The ground height (in p_nature's parent space) at world x, z over a generated
	// nature map, exactly on its triangles; NAN outside the map.
	static float height_at(Node3D *p_nature, float p_x, float p_z);
};
