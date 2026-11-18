/**************************************************************************/
/*  uzu_ui.cpp                                                            */
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

#include "uzu_ui.h"

#include "core/object/class_db.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box_texture.h"
#include "scene/resources/theme.h"

namespace {

Color color_of(const Variant &p_v, const Color &p_default) {
	if (p_v.get_type() == Variant::COLOR) {
		return p_v;
	}
	if (p_v.get_type() == Variant::STRING && Color::html_is_valid(p_v)) {
		return Color::html(p_v);
	}
	return p_default;
}

// Signed distance from p to a rounded rectangle centered at c with half size h and radius r.
float rounded_rect_distance(const Vector2 &p, const Vector2 &c, const Vector2 &h, float r) {
	const Vector2 q = (p - c).abs() - h + Vector2(r, r);
	return Vector2(MAX(q.x, 0.0f), MAX(q.y, 0.0f)).length() + MIN(MAX(q.x, q.y), 0.0f) - r;
}

Dictionary defaults() {
	Dictionary d;
	d["accent"] = Color::html("#d9a441");
	d["text"] = Color::html("#f4ead2");
	d["font_size"] = 18;
	Dictionary panel;
	panel["top"] = Color::html("#1d2740ee");
	panel["bottom"] = Color::html("#10162add");
	panel["radius"] = 12;
	panel["border_width"] = 2;
	panel["shadow"] = 8;
	panel["padding"] = 14;
	d["panel"] = panel;
	Dictionary button;
	button["top"] = Color::html("#34416a");
	button["bottom"] = Color::html("#253050");
	button["hover"] = Color::html("#46598f");
	button["pressed"] = Color::html("#1a2238");
	button["radius"] = 8;
	button["border_width"] = 1;
	button["shadow"] = 3;
	button["padding"] = 10;
	d["button"] = button;
	Dictionary bar;
	bar["bg"] = Color(0, 0, 0, 0.5);
	bar["radius"] = 6;
	d["bar"] = bar;
	return d;
}

Dictionary merged(const Dictionary &p_base, const Variant &p_over) {
	Dictionary out = p_base.duplicate();
	if (p_over.get_type() == Variant::DICTIONARY) {
		const Dictionary over = p_over;
		for (const Variant &k : over.keys()) {
			out[k] = over[k];
		}
	}
	return out;
}

} // namespace

Ref<Image> UZUUI::draw_panel(int p_radius, const Color &p_top, const Color &p_bottom, const Color &p_border, int p_border_width, int p_shadow) {
	const int radius = CLAMP(p_radius, 0, 64);
	const int border = CLAMP(p_border_width, 0, 16);
	const int shadow = CLAMP(p_shadow, 0, 32);
	// The rectangle sits in the middle, with room for the shadow around it and a stretchable
	// middle of 8 px; the corners are what the 9-slice keeps.
	const int corner = MAX(radius, border) + 1;
	const int w = 2 * (shadow + corner) + 8;
	const int h = w;
	const Vector2 center(w * 0.5f, h * 0.5f);
	const Vector2 half(w * 0.5f - shadow, h * 0.5f - shadow);
	const float drop = shadow * 0.35f; // The shadow falls a little downward.
	Ref<Image> img = Image::create_empty(w, h, false, Image::FORMAT_RGBA8);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const Vector2 p(x + 0.5f, y + 0.5f);
			const float d = rounded_rect_distance(p, center, half, radius);
			const float inside = CLAMP(0.5f - d, 0.0f, 1.0f); // Anti-aliased edge.
			// Fill: a vertical gradient over the rectangle; the border near the edge.
			const float t = CLAMP((p.y - (center.y - half.y)) / (2.0f * half.y), 0.0f, 1.0f);
			Color c = p_top.lerp(p_bottom, t);
			if (border > 0) {
				const float in_border = CLAMP(d + border + 0.5f, 0.0f, 1.0f);
				c = c.lerp(p_border, in_border * p_border.a);
				c.a = MAX(c.a, in_border * p_border.a);
			}
			c.a *= inside;
			// Shadow: soft, behind and below the rectangle.
			if (shadow > 0 && inside < 1.0f) {
				const float ds = rounded_rect_distance(p - Vector2(0, drop), center, half, radius);
				const float s = 1.0f - CLAMP(ds / shadow, 0.0f, 1.0f);
				const float sa = 0.45f * s * s;
				// Over-composite the fill onto the shadow.
				const float a = c.a + sa * (1.0f - c.a);
				if (a > 0.0f) {
					c = Color((c.r * c.a) / a, (c.g * c.a) / a, (c.b * c.a) / a, a);
				}
			}
			img->set_pixel(x, y, c);
		}
	}
	return img;
}

Ref<StyleBox> UZUUI::create_panel_style(const Dictionary &p_style) {
	const Color bg = color_of(p_style.get("bg", Variant()), Color(0.08, 0.1, 0.17, 0.87));
	const Color top = color_of(p_style.get("top", Variant()), bg);
	const Color bottom = color_of(p_style.get("bottom", Variant()), p_style.has("top") ? top : bg);
	const int radius = p_style.get("radius", 10);
	const int border_width = p_style.get("border_width", p_style.has("border") ? 2 : 0);
	const Color border = color_of(p_style.get("border", Variant()), Color(0, 0, 0, 0));
	const int shadow = p_style.get("shadow", 0);
	const float padding = p_style.get("padding", 10);
	Ref<Image> img = draw_panel(radius, top, bottom, border, border_width, shadow);
	Ref<StyleBoxTexture> sb;
	sb.instantiate();
	sb->set_texture(ImageTexture::create_from_image(img));
	const int s = CLAMP(shadow, 0, 32);
	const int corner = MAX(CLAMP(radius, 0, 64), CLAMP(border_width, 0, 16)) + 1;
	sb->set_texture_margin_all(s + corner);
	sb->set_expand_margin_all(s); // The shadow is drawn outside the control's rect.
	sb->set_content_margin_all(padding);
	return sb;
}

Dictionary UZUUI::get_theme_defaults() {
	return defaults();
}

Ref<Theme> UZUUI::create_theme(const Dictionary &p_options) {
	const Dictionary o = merged(defaults(), p_options);
	const Color accent = color_of(o["accent"], Color::html("#d9a441"));
	const Color text = color_of(o["text"], Color::html("#f4ead2"));
	Ref<Theme> theme;
	theme.instantiate();
	theme->set_default_font_size(o["font_size"]);
	const Ref<Font> font = o.get("font", Variant());
	if (font.is_valid()) {
		theme->set_default_font(font);
	}
	// Panels: PanelContainer, TabContainer pages, lists, tooltips.
	Dictionary panel = merged(Dictionary(defaults()["panel"]), o["panel"]);
	if (!panel.has("border")) {
		panel["border"] = accent;
	}
	const Ref<StyleBox> panel_style = create_panel_style(panel);
	theme->set_stylebox("panel", "PanelContainer", panel_style);
	theme->set_stylebox("panel", "TabContainer", panel_style);
	theme->set_stylebox("panel", "ItemList", panel_style);
	Dictionary tip = panel.duplicate();
	tip["padding"] = 8;
	tip["shadow"] = 4;
	theme->set_stylebox("panel", "TooltipPanel", create_panel_style(tip));
	// Buttons: normal, hover, pressed, disabled.
	Dictionary button = merged(Dictionary(defaults()["button"]), o["button"]);
	if (!button.has("border")) {
		button["border"] = accent;
	}
	theme->set_stylebox("normal", "Button", create_panel_style(button));
	Dictionary state = button.duplicate();
	state["top"] = color_of(button["hover"], Color::html("#46598f"));
	state["bottom"] = Color(state["top"]).darkened(0.15);
	theme->set_stylebox("hover", "Button", create_panel_style(state));
	state["top"] = color_of(button["pressed"], Color::html("#1a2238"));
	state["bottom"] = state["top"];
	state["shadow"] = 0;
	theme->set_stylebox("pressed", "Button", create_panel_style(state));
	state["top"] = Color(color_of(button["top"], Color::html("#34416a"))).darkened(0.4);
	state["bottom"] = state["top"];
	state["border"] = Color(accent, 0.3);
	theme->set_stylebox("disabled", "Button", create_panel_style(state));
	Ref<StyleBoxEmpty> none;
	none.instantiate();
	theme->set_stylebox("focus", "Button", none);
	// Tabs: the selected tab joins the page; the others are darker.
	Dictionary tab = button.duplicate();
	tab["shadow"] = 0;
	tab["padding"] = 8;
	theme->set_stylebox("tab_selected", "TabContainer", create_panel_style(tab));
	tab["top"] = Color(color_of(button["top"], Color::html("#34416a"))).darkened(0.35);
	tab["bottom"] = tab["top"];
	theme->set_stylebox("tab_unselected", "TabContainer", create_panel_style(tab));
	tab["top"] = color_of(button["hover"], Color::html("#46598f"));
	tab["bottom"] = tab["top"];
	theme->set_stylebox("tab_hovered", "TabContainer", create_panel_style(tab));
	theme->set_stylebox("tabbar_background", "TabContainer", none);
	// Bars and list selection.
	const Dictionary bar = merged(Dictionary(defaults()["bar"]), o["bar"]);
	Ref<StyleBoxFlat> bar_bg;
	bar_bg.instantiate();
	bar_bg->set_bg_color(color_of(bar["bg"], Color(0, 0, 0, 0.5)));
	bar_bg->set_corner_radius_all(bar["radius"]);
	theme->set_stylebox("background", "ProgressBar", bar_bg);
	Ref<StyleBoxFlat> fill = bar_bg->duplicate();
	fill->set_bg_color(color_of(bar.get("fill", Variant()), accent));
	theme->set_stylebox("fill", "ProgressBar", fill);
	Ref<StyleBoxFlat> selected = bar_bg->duplicate();
	selected->set_bg_color(Color(accent, 0.35));
	theme->set_stylebox("selected", "ItemList", selected);
	theme->set_stylebox("selected_focus", "ItemList", selected);
	theme->set_stylebox("cursor", "ItemList", none);
	theme->set_stylebox("cursor_unfocused", "ItemList", none);
	theme->set_stylebox("focus", "ItemList", none);
	// Text.
	for (const char *cls : { "Label", "Button", "ProgressBar", "TooltipLabel", "ItemList" }) {
		theme->set_color("font_color", cls, text);
	}
	theme->set_color("default_color", "RichTextLabel", text);
	for (const char *name : { "font_hover_color", "font_pressed_color", "font_focus_color", "font_hover_pressed_color" }) {
		theme->set_color(name, "Button", text);
	}
	theme->set_color("font_disabled_color", "Button", Color(text, 0.4));
	theme->set_color("font_selected_color", "TabContainer", text);
	theme->set_color("font_hovered_color", "TabContainer", text);
	theme->set_color("font_unselected_color", "TabContainer", Color(text, 0.65));
	theme->set_color("font_selected_color", "ItemList", text);
	theme->set_color("font_hovered_color", "ItemList", text);
	theme->set_constant("separation", "VBoxContainer", 8);
	theme->set_constant("separation", "HBoxContainer", 10);
	return theme;
}

Control *UZUUI::make_panel(Control *p_content) {
	PanelContainer *panel = memnew(PanelContainer);
	panel->set_name("Panel");
	if (p_content) {
		panel->add_child(p_content);
	}
	return panel;
}

Control *UZUUI::make_button(const String &p_text) {
	Button *b = memnew(Button);
	b->set_name("Button");
	b->set_text(p_text);
	return b;
}

Control *UZUUI::make_bar(double p_value, double p_max) {
	ProgressBar *bar = memnew(ProgressBar);
	bar->set_name("Bar");
	bar->set_max(p_max);
	bar->set_value(p_value);
	bar->set_show_percentage(false);
	bar->set_custom_minimum_size(Size2(0, 16));
	return bar;
}

Control *UZUUI::make_tabs(const PackedStringArray &p_titles) {
	TabContainer *tabs = memnew(TabContainer);
	tabs->set_name("Tabs");
	for (const String &title : p_titles) {
		VBoxContainer *page = memnew(VBoxContainer);
		page->set_name(title.validate_node_name());
		tabs->add_child(page, true);
	}
	return tabs;
}

Control *UZUUI::make_list(const PackedStringArray &p_items) {
	ItemList *list = memnew(ItemList);
	list->set_name("List");
	for (const String &item : p_items) {
		list->add_item(item);
	}
	list->set_auto_height(true);
	return list;
}

Control *UZUUI::make_dialog(const String &p_title, const String &p_text, const PackedStringArray &p_buttons) {
	VBoxContainer *box = memnew(VBoxContainer);
	box->set_name("Content");
	Label *title = memnew(Label);
	title->set_name("Title");
	title->set_text(p_title);
	title->add_theme_font_size_override("font_size", 22);
	box->add_child(title);
	Label *text = memnew(Label);
	text->set_name("Text");
	text->set_text(p_text);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text->set_custom_minimum_size(Size2(360, 0));
	box->add_child(text);
	HBoxContainer *row = memnew(HBoxContainer);
	row->set_name("Buttons");
	row->set_alignment(BoxContainer::ALIGNMENT_END);
	for (const String &label : p_buttons) {
		Control *b = make_button(label);
		b->set_name(label.validate_node_name());
		row->add_child(b, true);
	}
	box->add_child(row);
	Control *panel = make_panel(box);
	panel->set_name("Dialog");
	return panel;
}

void UZUUI::_bind_methods() {
	ClassDB::bind_static_method("UZUUI", D_METHOD("create_panel_style", "style"), &UZUUI::create_panel_style);
	ClassDB::bind_static_method("UZUUI", D_METHOD("draw_panel", "radius", "top", "bottom", "border", "border_width", "shadow"), &UZUUI::draw_panel);
	ClassDB::bind_static_method("UZUUI", D_METHOD("create_theme", "options"), &UZUUI::create_theme, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("UZUUI", D_METHOD("get_theme_defaults"), &UZUUI::get_theme_defaults);
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_panel", "content"), &UZUUI::make_panel, DEFVAL(Variant()));
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_button", "text"), &UZUUI::make_button);
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_bar", "value", "max"), &UZUUI::make_bar, DEFVAL(100.0));
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_tabs", "titles"), &UZUUI::make_tabs);
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_list", "items"), &UZUUI::make_list);
	ClassDB::bind_static_method("UZUUI", D_METHOD("make_dialog", "title", "text", "buttons"), &UZUUI::make_dialog);
}
