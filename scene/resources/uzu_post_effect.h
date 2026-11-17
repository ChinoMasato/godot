/**************************************************************************/
/*  uzu_post_effect.h                                                     */
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

// U.ZU post effect (BACKLOG B-703b): a CompositorEffect that strengthens the
// light-dark contrast at edges (an unsharp mask on the HDR color) and darkens
// the corners (vignette), the painterly "メリハリ" and focus from the look plan.
// Runs on RenderingDevice renderers (Forward+, Mobile) with a 16-bit float color
// buffer; it does nothing in the Compatibility renderer or without a GPU.
class UZUPostEffect : public CompositorEffect {
	GDCLASS(UZUPostEffect, CompositorEffect);

	float edge_strength = 0.3;
	float vignette_strength = 0.35;

	// Render-thread resources, created on first use.
	RID shader;
	RID pipeline;
	RID scratch;
	Size2i scratch_size;
	bool shader_failed = false;

	void _render(int p_effect_callback_type, const RenderData *p_render_data);
	bool _ensure_pipeline();

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_edge_strength(float p_strength);
	float get_edge_strength() const;
	void set_vignette_strength(float p_strength);
	float get_vignette_strength() const;

	UZUPostEffect();
	~UZUPostEffect();
};
