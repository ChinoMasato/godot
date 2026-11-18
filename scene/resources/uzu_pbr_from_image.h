/**************************************************************************/
/*  uzu_pbr_from_image.h                                                  */
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

// U.ZU PBR from an image (BACKLOG B-902, U.ZU Picture P-2): one texture image becomes
// estimated PBR maps: albedo (the slow, baked-in light gradient taken out), normal
// (OpenGL, as Godot expects), ORM (AO, roughness, metallic), height, and an
// ORMMaterial3D that uses them. The strengths per kind of material (stone, brick, ...)
// were fitted against scanned materials (Poly Haven) in the prototype
// (ai_loop/prototypes/picture/pbr_tool.gd), and this is a port of it.
// Honest limit (measured there): a normal map guessed from brightness is about as far
// from a scan as a flat normal map is; a normal map from an outside model (brought in
// with UZUMapImport) can replace it.
class UZUPBRFromImage : public Object {
	GDCLASS(UZUPBRFromImage, Object);

protected:
	static void _bind_methods();

public:
	// {kind: {"strength", "rough", "spread", "metal", "detail"}}.
	static Dictionary get_kinds();
	// Options: "strength" (1.0, times the kind's), "delight" (0.7, 0..1), "tile" (false:
	// the image repeats, so blurs and differences wrap), "invert" (false: dark is high),
	// "size" (1024, the largest width), "normal" (an Image to use as the normal map).
	// Returns {"ok", "kind", "size", "albedo", "normal", "orm", "height" (Images),
	// "material" (ORMMaterial3D), "normal_source", "stats", "warnings", "note"}.
	static Dictionary make(const Ref<Image> &p_image, const String &p_kind = "stone", const Dictionary &p_options = Dictionary());
	// The same with files: albedo.png, normal.png, orm.png, height.png and material.tres in
	// p_out_dir. Option "normal_from" is a path to a normal map. Returns "files" instead
	// of the images and the material.
	static Dictionary make_file(const String &p_in_path, const String &p_out_dir, const String &p_kind = "stone", const Dictionary &p_options = Dictionary());
	// Compares a normal map with a reference one (a scan): the mean angle between them,
	// and between a flat normal map and the reference, in degrees. With two ORM/ARM maps,
	// also their roughness (green) means and correlation.
	static Dictionary compare(const Ref<Image> &p_normal, const Ref<Image> &p_reference, const Ref<Image> &p_orm = Ref<Image>(), const Ref<Image> &p_reference_orm = Ref<Image>());
};
