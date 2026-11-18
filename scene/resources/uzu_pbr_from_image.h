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
/* the "Software" to deal in the Software without restriction, including  */
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
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"

// U.ZU PBR from a single image (BACKLOG B-902, U.ZU Picture P-2): estimates the
// PBR maps of one texture photograph in the engine: albedo with the baked light
// removed, a normal map from the image's own brightness, an ORM map (ambient
// occlusion in cavities, roughness per material kind, metallic) and a height
// map. The per-kind values were fitted against Poly Haven scans in the
// prototype (ai_loop/prototypes/picture/pbr_tool.gd); this class is its C++
// port with the same math. Be honest about the premise: a normal map estimated
// from one image measures about as close to a real scan as a flat normal does,
// so treat the output as a starting point, not a scan. An externally estimated
// normal map can replace the brightness-based one (p_normal_from), ideally
// after UZUMapImport has checked it.
class UZUPBRFromImage : public Object {
	GDCLASS(UZUPBRFromImage, Object);

protected:
	static void _bind_methods();

public:
	// The fitted per-kind table: {kind: {strength, rough, spread, metal, detail}}.
	static Dictionary kinds();
	// Generates the maps from p_image. Returns {"ok", "kind", "size",
	// "albedo", "normal", "orm", "height" (Ref<Image> each), "normal_source",
	// "stats", "warnings"}, or {"ok": false, "error"}.
	static Dictionary make(const Ref<Image> &p_image, const String &p_kind, float p_strength = 1.0f, float p_delight = 0.7f, bool p_tile = false, bool p_invert = false, const Ref<Image> &p_normal_from = Ref<Image>(), int p_max_size = 1024);
	// The same with files: loads p_in_path, writes albedo.png, normal.png,
	// orm.png, height.png and material.tres (an ORMMaterial3D using the maps)
	// into p_out_dir. Returns the same Dictionary with "files" in place of the
	// images.
	static Dictionary make_file(const String &p_in_path, const String &p_out_dir, const String &p_kind, float p_strength = 1.0f, float p_delight = 0.7f, bool p_tile = false, bool p_invert = false, const String &p_normal_from_path = String(), int p_max_size = 1024);
	// Measures a normal map against a reference (e.g. a scan): mean angular
	// error in degrees, plus the same error for a flat normal as the honest
	// baseline. With p_rough and p_ref_rough (ORM maps), also reports the
	// roughness means and their correlation.
	static Dictionary compare(const Ref<Image> &p_normal, const Ref<Image> &p_ref, const Ref<Image> &p_rough = Ref<Image>(), const Ref<Image> &p_ref_rough = Ref<Image>());
};
