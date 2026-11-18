/**************************************************************************/
/*  uzu_map_import.h                                                      */
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

// U.ZU map import (BACKLOG B-903, U.ZU Picture P-3): brings in normal and depth maps
// made outside the engine (by any AI model or tool; no model ships with the engine)
// and makes them safe to use with a picture. It checks and fixes the size (resized to
// the picture), a small offset (edge cross-correlation against the picture), the normal
// map convention (OpenGL vs DirectX, judged from the picture's own shading; Godot
// expects OpenGL), vector length, the depth direction (white = near or far, judged
// from the bottom vs the top of the picture) and a narrow depth range. It reports what
// it found and what it changed, so an agent can trust or override it.
class UZUMapImport : public Object {
	GDCLASS(UZUMapImport, Object);

protected:
	static void _bind_methods();

public:
	enum MapKind {
		MAP_NORMAL,
		MAP_DEPTH,
	};
	enum DepthKind {
		DEPTH_AUTO, // Judge white = near or far from the picture.
		DEPTH_DISPARITY, // White is near already.
		DEPTH_LINEAR, // White is far: invert.
	};

	// Returns {"ok", "image" (the fixed map, RGB8 normal or L8 depth, the picture's
	// size), "found", "changed", "warnings"}.
	static Dictionary import_map(const Ref<Image> &p_map, const Ref<Image> &p_picture, MapKind p_kind, DepthKind p_depth_kind = DEPTH_AUTO, bool p_align = true);
	// The same with files: loads p_map_path and p_picture_path, saves the fixed map as a
	// PNG to p_out_path. Returns the same Dictionary with "out" instead of "image".
	static Dictionary import_file(const String &p_map_path, const String &p_picture_path, const String &p_out_path, MapKind p_kind, DepthKind p_depth_kind = DEPTH_AUTO, bool p_align = true);
};

VARIANT_ENUM_CAST(UZUMapImport::MapKind);
VARIANT_ENUM_CAST(UZUMapImport::DepthKind);
