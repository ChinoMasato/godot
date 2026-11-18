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

TEST_CASE("[SceneTree][UZUUI] World frames: texture that repeats, shapes, ornaments") {
	Dictionary wood;
	wood["material"] = "wood";
	wood["base"] = "#6b4a2e";
	wood["edge"] = "#2e1d10";
	wood["accent"] = "#e6c36a";
	wood["ornament"] = "brackets";
	wood["radius"] = 10;
	wood["border_width"] = 3;
	wood["shadow"] = 8;
	const Ref<Image> img = UZUUI::draw_frame(wood);
	const int w = img->get_width();
	const int m = (w - 32) / 2; // The margin the 9-slice keeps; the middle 32 px repeat.
	// The middle repeats every 32 px: the row through the middle matches itself 32 px on.
	const int y = w / 2;
	for (int x = m; x < m + 4; x++) {
		CHECK(close(img->get_pixel(x, y), img->get_pixel(x + 32, y)));
	}
	// The wood is not flat: the middle has some variation.
	float lo = 1.0f, hi = 0.0f;
	for (int x = m; x < m + 32; x++) {
		const float l = img->get_pixel(x, y).get_luminance();
		lo = MIN(lo, l);
		hi = MAX(hi, l);
	}
	CHECK(hi - lo > 0.03f);
	// A bracket ornament in the accent color sits in the top-left corner.
	bool gold = false;
	for (int yy = 8; yy < m; yy++) {
		for (int x = 8; x < m; x++) {
			gold = gold || close(img->get_pixel(x, yy), Color::html("#e6c36a"));
		}
	}
	CHECK(gold);

	// Pixel frames: no half-transparent edge pixels inside the frame's own area.
	Dictionary pixel;
	pixel["shape"] = "pixel";
	pixel["shadow"] = 0;
	pixel["base"] = "#1b1f3a";
	pixel["edge"] = "#f4f4f4";
	const Ref<Image> px = UZUUI::draw_frame(pixel);
	int partial = 0;
	for (int yy = 0; yy < px->get_height(); yy++) {
		for (int x = 0; x < px->get_width(); x++) {
			const float a = px->get_pixel(x, yy).a;
			partial += a > 0.02f && a < 0.98f;
		}
	}
	CHECK(partial == 0);
	CHECK(px->get_pixel(0, 0).a < 0.02f); // The notched corner.

	// Glass: the edge glows in the accent color, brighter near the edge than in the middle.
	Dictionary glass;
	glass["material"] = "glass";
	glass["base"] = "#07141e";
	glass["accent"] = "#38e1ff";
	glass["shape"] = "chamfer";
	glass["shadow"] = 0;
	const Ref<Image> gl = UZUUI::draw_frame(glass);
	const int gm = gl->get_width() / 2;
	CHECK(gl->get_pixel(gm, 4).b > gl->get_pixel(gm, gm).b);
	CHECK(gl->get_pixel(0, 0).a < 0.02f); // The cut corner.
}

TEST_CASE("[SceneTree][UZUUI] World themes and game parts") {
	const PackedStringArray worlds = UZUUI::get_world_names();
	CHECK(worlds.size() == 6);
	for (const String &world : worlds) {
		const Ref<Theme> theme = UZUUI::create_world_theme(world);
		REQUIRE_MESSAGE(theme.is_valid(), world);
		for (const char *variation : { "UZUGauge", "UZUTalk", "UZUNameTag", "UZUSlot", "UZURibbon" }) {
			CHECK_MESSAGE(theme->is_type_variation(variation, String(variation) == "UZUGauge" ? "ProgressBar" : "PanelContainer"), world);
		}
		CHECK(String(theme->get_meta("uzu_world")) == world);
		CHECK(Dictionary(theme->get_meta("uzu_motion")).has("duration"));
	}
	// Different worlds, different frames.
	const Ref<StyleBoxTexture> fantasy = UZUUI::create_world_theme("fantasy")->get_stylebox("panel", "PanelContainer");
	const Ref<StyleBoxTexture> sf = UZUUI::create_world_theme("sf")->get_stylebox("panel", "PanelContainer");
	REQUIRE(fantasy.is_valid());
	REQUIRE(sf.is_valid());
	CHECK(fantasy->get_texture()->get_image()->get_data() != sf->get_texture()->get_image()->get_data());
	CHECK(fantasy->get_h_axis_stretch_mode() == StyleBoxTexture::AXIS_STRETCH_MODE_TILE_FIT);
	// Motion follows the world: persona snaps with a big overshoot, pixel steps, sf has none.
	CHECK(double(UZUUI::get_world_motion("persona")["overshoot"]) > double(UZUUI::get_world_motion("fantasy")["overshoot"]));
	CHECK(int(UZUUI::get_world_motion("pixel")["steps"]) > 0);
	CHECK(double(UZUUI::get_world_motion("sf")["overshoot"]) == doctest::Approx(0.0));
	ERR_PRINT_OFF;
	CHECK(UZUUI::create_world_theme("steampunk").is_null());
	ERR_PRINT_ON;

	Control *gauge = UZUUI::make_gauge(30, 60, 12);
	CHECK(gauge->get_theme_type_variation() == "UZUGauge");
	CHECK(gauge->get_custom_minimum_size().x == doctest::Approx(144));
	Control *talk = UZUUI::make_talk_window("Elder", "Bring three shards.");
	CHECK(Object::cast_to<Label>(talk->get_node(NodePath("TagMargin/NameTag/Speaker")))->get_text() == "Elder");
	CHECK(Object::cast_to<Label>(talk->get_node(NodePath("Window/Body/Line")))->get_text() == "Bring three shards.");
	CHECK(Object::cast_to<Label>(talk->get_node(NodePath("Window/Body/Next")))->get_text() == String::utf8("▼"));
	Control *slot = UZUUI::make_item_slot("Key", 3);
	CHECK(Object::cast_to<Label>(slot->get_node(NodePath("Box/Count")))->get_text() == "3");
	CHECK(slot->get_theme_type_variation() == "UZUSlot");
	Control *ribbon = UZUUI::make_ribbon("Chapter 1");
	CHECK(Object::cast_to<Label>(ribbon->get_node(NodePath("Title")))->get_text() == "Chapter 1");
	Control *sep = UZUUI::make_separator();
	CHECK(sep->get_custom_minimum_size().y == doctest::Approx(12));
	for (Control *c : { gauge, talk, slot, ribbon, sep }) {
		memdelete(c);
	}
}

} // namespace TestUZUUI
