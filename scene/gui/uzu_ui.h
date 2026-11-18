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

	// Parts. Each returns a new Control, not yet added anywhere.
	static Control *make_panel(Control *p_content = nullptr);
	static Control *make_button(const String &p_text);
	static Control *make_bar(double p_value, double p_max = 100.0);
	static Control *make_tabs(const PackedStringArray &p_titles);
	static Control *make_list(const PackedStringArray &p_items);
	static Control *make_dialog(const String &p_title, const String &p_text, const PackedStringArray &p_buttons);
};
