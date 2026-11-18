/**************************************************************************/
/*  uzu_palette_dither.h                                                  */
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

#include "scene/resources/compositor.h"

class RenderData;

// U.ZU palette quantization with ordered dithering (BACKLOG B-721): reduces the
// colors to a few levels per channel and breaks the bands with a 4x4 Bayer
// pattern, for a "modern retro" finish. Runs before tonemapping (where compositor
// effects run), so it quantizes in a display-like gamma space. Runs on
// RenderingDevice renderers (Forward+, Mobile); nothing in Compatibility.
class UZUPaletteDither : public CompositorEffect {
	GDCLASS(UZUPaletteDither, CompositorEffect);

	int levels = 6; // Per channel: 6 x 6 x 6 = 216 colors.
	float dither_strength = 1.0; // 0 = plain banding, 1 = a full step of dither.
	int dither_scale = 1; // Screen pixels per dither cell (bigger = chunkier pattern).

	// Render-thread resources, created on first use.
	RID shader;
	RID pipeline;
	bool shader_failed = false;

	void _render(int p_effect_callback_type, const RenderData *p_render_data);
	bool _ensure_pipeline();

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_levels(int p_levels);
	int get_levels() const;
	void set_dither_strength(float p_strength);
	float get_dither_strength() const;
	void set_dither_scale(int p_scale);
	int get_dither_scale() const;

	// The Bayer threshold of a screen pixel (in [-0.5, 0.5)), and the color the
	// shader produces there; the shader uses the same formulas.
	float get_dither_threshold(const Vector2i &p_pixel) const;
	Color quantize(const Color &p_color, const Vector2i &p_pixel) const;

	UZUPaletteDither();
	~UZUPaletteDither();
};
