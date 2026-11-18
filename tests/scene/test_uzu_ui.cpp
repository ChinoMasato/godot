/**************************************************************************/
/*  test_uzu_ui.cpp                                                       */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_uzu_ui)

#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/uzu_ui.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box_texture.h"
#include "scene/resources/theme.h"

namespace TestUZUUI {

// The panel is an 8-bit image: compare colors to within one step or so.
static bool close(const Color &p_a, const Color &p_b) {
	return Math::abs(p_a.r - p_b.r) < 0.01f && Math::abs(p_a.g - p_b.g) < 0.01f && Math::abs(p_a.b - p_b.b) < 0.01f;
}

TEST_CASE("[SceneTree][UZUUI] A procedural panel: gradient, rounded corners, border, shadow") {
	const Color top(0.2, 0.3, 0.6);
	const Color bottom(0.05, 0.05, 0.15);
	const Color border(0.9, 0.7, 0.2);
	const Ref<Image> img = UZUUI::draw_panel(12, top, bottom, border, 2, 8);
	REQUIRE(img.is_valid());
	const int w = img->get_width();
	const int h = img->get_height();
	CHECK(w == 2 * (8 + 13) + 8);
	CHECK(h == w);
	// The middle: opaque, between the two gradient colors, lighter above than below.
	const Color mid = img->get_pixel(w / 2, h / 2);
	CHECK(mid.a > 0.99f);
	CHECK(img->get_pixel(w / 2, 8 + 4).get_luminance() > img->get_pixel(w / 2, h - 8 - 4).get_luminance());
	// The border: on the straight edge, the border color.
	const Color edge = img->get_pixel(w / 2, 8);
	CHECK(close(edge, border));
	CHECK(edge.a > 0.9f);
	// The rounded corner: the very corner of the rectangle is not filled (only shadow).
	const Color corner = img->get_pixel(8, 8);
	CHECK(corner.a < 0.5f);
	// The shadow: soft and semi-transparent outside, stronger below than above, gone at the image edge.
	CHECK(img->get_pixel(w / 2, h - 5).a > img->get_pixel(w / 2, 4).a);
	CHECK(img->get_pixel(w / 2, h - 5).a < 0.45f);
	CHECK(img->get_pixel(0, 0).a < 0.02f);
	// No shadow, no border: nothing outside the rectangle, and the edge is the fill.
	const Ref<Image> plain = UZUUI::draw_panel(0, top, top, Color(0, 0, 0, 0), 0, 0);
	CHECK(close(plain->get_pixel(0, 0), top));
	CHECK(plain->get_pixel(0, 0).a > 0.99f);
}

TEST_CASE("[SceneTree][UZUUI] Panel styles and the theme") {
	Dictionary style;
	style["top"] = "#334466";
	style["bottom"] = "#112233";
	style["radius"] = 10;
	style["border"] = "#d9a441";
	style["shadow"] = 6;
	style["padding"] = 14;
	Ref<StyleBoxTexture> sb = UZUUI::create_panel_style(style);
	REQUIRE(sb.is_valid());
	CHECK(sb->get_texture().is_valid());
	CHECK(sb->get_texture_margin(SIDE_LEFT) == doctest::Approx(6 + 11));
	CHECK(sb->get_expand_margin(SIDE_BOTTOM) == doctest::Approx(6)); // The shadow is outside the rect.
	CHECK(sb->get_content_margin(SIDE_TOP) == doctest::Approx(14));

	Dictionary options;
	options["accent"] = "#40c0a0";
	options["font_size"] = 22;
	const Ref<Theme> theme = UZUUI::create_theme(options);
	REQUIRE(theme.is_valid());
	CHECK(theme->get_default_font_size() == 22);
	for (const char *item : { "panel/PanelContainer", "panel/TabContainer", "panel/ItemList", "panel/TooltipPanel", "normal/Button", "hover/Button",
				 "pressed/Button", "disabled/Button", "tab_selected/TabContainer", "tab_unselected/TabContainer", "background/ProgressBar", "fill/ProgressBar", "selected/ItemList" }) {
		const String s = item;
		CHECK_MESSAGE(theme->has_stylebox(s.get_slicec('/', 0), s.get_slicec('/', 1)), item);
	}
	// The accent colors the bar fill.
	Ref<StyleBoxFlat> fill = theme->get_stylebox("fill", "ProgressBar");
	REQUIRE(fill.is_valid());
	CHECK(fill->get_bg_color().is_equal_approx(Color::html("#40c0a0")));
	CHECK(UZUUI::get_theme_defaults().has("panel"));
}

TEST_CASE("[SceneTree][UZUUI] Parts") {
	Control *button = UZUUI::make_button("Start");
	CHECK(Object::cast_to<Button>(button)->get_text() == "Start");
	Control *bar = UZUUI::make_bar(30, 60);
	CHECK(Object::cast_to<ProgressBar>(bar)->get_value() == doctest::Approx(30));
	CHECK(Object::cast_to<ProgressBar>(bar)->get_max() == doctest::Approx(60));
	Control *tabs = UZUUI::make_tabs({ "Items", "Skills", "Map" });
	CHECK(Object::cast_to<TabContainer>(tabs)->get_tab_count() == 3);
	CHECK(Object::cast_to<TabContainer>(tabs)->get_tab_title(1) == "Skills");
	Control *list = UZUUI::make_list({ "Sword", "Shield" });
	CHECK(Object::cast_to<ItemList>(list)->get_item_count() == 2);
	Control *dialog = UZUUI::make_dialog("Elder", "Bring three shards.", { "Accept", "Later" });
	REQUIRE(Object::cast_to<PanelContainer>(dialog) != nullptr);
	CHECK(Object::cast_to<Label>(dialog->get_node(NodePath("Content/Title")))->get_text() == "Elder");
	CHECK(dialog->get_node(NodePath("Content/Buttons/Accept")) != nullptr);
	CHECK(dialog->get_node(NodePath("Content/Buttons/Later")) != nullptr);
	Control *wrapped = UZUUI::make_panel(UZUUI::make_button("Inside"));
	CHECK(wrapped->get_child_count() == 1);
	for (Control *c : { button, bar, tabs, list, dialog, wrapped }) {
		memdelete(c);
	}
}

} // namespace TestUZUUI
