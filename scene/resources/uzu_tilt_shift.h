/**************************************************************************/
/*  uzu_tilt_shift.h                                                      */
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

// U.ZU tilt-shift (BACKLOG B-715): a CompositorEffect that keeps a horizontal
// band of the screen sharp and blurs above and below it, more the farther from
// the band, like a tilt-shift lens. Scenes read as miniature dioramas (the core
// of the HD-2D look). Runs on RenderingDevice renderers (Forward+, Mobile) with
// a 16-bit float color buffer; it does nothing in the Compatibility renderer.
class UZUTiltShift : public CompositorEffect {
	GDCLASS(UZUTiltShift, CompositorEffect);

	float focus_center = 0.55; // Screen height fraction, from the top.
	float focus_width = 0.12; // Half height of the sharp band.
	float transition = 0.25; // Distance over which the blur reaches its maximum.
	float max_blur = 6.0; // Blur radius in pixels at 1080 lines (scaled with the screen).

	// Render-thread resources, created on first use.
	RID shader;
	RID pipeline;
	RID scratch[2];
	Size2i scratch_size;
	bool shader_failed = false;

	void _render(int p_effect_callback_type, const RenderData *p_render_data);
	bool _ensure_pipeline();
	void _free_scratch();

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_focus_center(float p_center);
	float get_focus_center() const;
	void set_focus_width(float p_width);
	float get_focus_width() const;
	void set_transition(float p_transition);
	float get_transition() const;
	void set_max_blur(float p_pixels);
	float get_max_blur() const;

	// The blur radius in pixels at a screen height fraction (0 top, 1 bottom) for a
	// screen of p_screen_height lines; the shader uses the same formula.
	float get_blur_radius_at(float p_screen_y, float p_screen_height = 1080.0) const;

	UZUTiltShift();
	~UZUTiltShift();
};
