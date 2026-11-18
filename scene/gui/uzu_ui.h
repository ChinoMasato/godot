/**************************************************************************/
/*  uzu_ui.h                                                              */
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
#include "core/variant/typed_array.h"

class Control;
class StyleBox;
class Theme;

// U.ZU UI kit (BACKLOG B-1302, Phase 13 rich UI): panels drawn procedurally (a vertical
// gradient, rounded corners, a border and a soft shadow, in one 9-slice image), a theme
// that styles every common control with them, and constructors for the usual parts
// (panel, button, bar, tabs, list, dialog), so an agent gets a finished look without
// drawing assets. ai_loop/ui_tool.gd (B-1301) builds screens with it.
class UZUUI : public Object {
	GDCLASS(UZUUI, Object);

protected:
	static void _bind_methods();

public:
	// A panel style. Keys: "bg" (a color, or "top"/"bottom" for a gradient), "radius",
	// "border", "border_width", "shadow" (px), "padding". Colors are Color or "#rrggbb[aa]".
	static Ref<StyleBox> create_panel_style(const Dictionary &p_style);
	// The 9-slice image behind a panel style (exposed for tests and tools).
	static Ref<Image> draw_panel(int p_radius, const Color &p_top, const Color &p_bottom, const Color &p_border, int p_border_width, int p_shadow);
	// A theme for every common control. Keys (all optional): "accent", "text", "font"
	// (a Font), "font_size", and "panel", "button", "bar" (style dictionaries as above;
	// buttons also take "hover" and "pressed" colors).
	static Ref<Theme> create_theme(const Dictionary &p_options = Dictionary());
	static Dictionary get_theme_defaults();

	// World UI (B-1307): textured game frames instead of plain app panels. A frame style:
	// "material" (wood, metal, parchment, stone, glass, halftone, grime, flat), "shape"
	// (rounded, chamfer, skew, pixel), "base" and "edge" colors, "accent" (ornaments and
	// glow), "radius", "border_width", "shadow", "ornament" (none, rivets, nails, brackets,
	// diamonds), "padding", "seed". The middle of the image repeats, so the texture keeps
	// its scale at any size.
	static Ref<Image> draw_frame(const Dictionary &p_style);
	static Ref<StyleBox> create_frame_style(const Dictionary &p_style);
	// World themes: fantasy, wafu, sf, horror, persona, pixel. One name switches the
	// frames, colors and parts; p_overrides changes any option of the world.
	static PackedStringArray get_world_names();
	static Dictionary get_world_options(const String &p_world);
	static Ref<Theme> create_world_theme(const String &p_world, const Dictionary &p_overrides = Dictionary());
	// How the world moves (for open/hover animation): {"duration", "transition", "ease",
	// "overshoot", "steps"}; read by UI motion (B-1304).
	static Dictionary get_world_motion(const String &p_world);

	// Game parts, styled by the theme's type variations (UZUGauge, UZUTalk, UZUNameTag,
	// UZUSlot, UZURibbon, UZUSeparator), so they change with the world.
	static Control *make_gauge(double p_value, double p_max = 100.0, int p_segments = 10);
	static Control *make_talk_window(const String &p_speaker, const String &p_text);
	static Control *make_item_slot(const String &p_label = String(), int p_count = 0);
	static Control *make_ribbon(const String &p_text);
	static Control *make_separator();

	// Parts. Each returns a new Control, not yet added anywhere.
	static Control *make_panel(Control *p_content = nullptr);
	static Control *make_button(const String &p_text);
	static Control *make_bar(double p_value, double p_max = 100.0);
	static Control *make_tabs(const PackedStringArray &p_titles);
	static Control *make_list(const PackedStringArray &p_items);
	static Control *make_dialog(const String &p_title, const String &p_text, const PackedStringArray &p_buttons);
};
