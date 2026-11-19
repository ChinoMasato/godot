/**************************************************************************/
/*  uzu_vfx.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* the Software"), to deal in the Software without restriction, including  */
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

#include "uzu_vfx.h"

#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/io/image.h"
#include "core/templates/vector.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/uzu_juice.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/uzu_sfx.h"
#include "servers/audio/audio_server.h"

// ---------------------------------------------------------------------------
// Recipe validation (pure). A part spec: the keys it accepts, with the
// default and the range of each numeric key. Colors travel as [r, g, b] or
// [r, g, b, a] arrays (a Color value is accepted too, for recipes built in
// code).

struct UZUVfxNumKey {
	const char *name;
	double def;
	double min;
	double max;
	bool is_int;
};

struct UZUVfxPartSpec {
	const char *type;
	const UZUVfxNumKey *nums;
	int num_count;
	bool has_color;
	Color color_def;
	bool has_at;
	bool has_preset;
};

static const UZUVfxNumKey _sparks_nums[] = {
	{ "count", 24, 1, 512, true },
	{ "power", 1.0, 0.1, 8.0, false },
	{ "lifetime", 0.45, 0.05, 10.0, false },
	{ "size", 0.06, 0.01, 1.0, false },
};
static const UZUVfxNumKey _smoke_nums[] = {
	{ "count", 12, 1, 256, true },
	{ "lifetime", 1.6, 0.1, 20.0, false },
	{ "size", 0.8, 0.1, 4.0, false },
	{ "rise", 0.9, 0.0, 5.0, false },
};
static const UZUVfxNumKey _debris_nums[] = {
	{ "count", 12, 1, 256, true },
	{ "power", 1.2, 0.1, 8.0, false },
	{ "size", 0.07, 0.02, 0.5, false },
	{ "lifetime", 0.9, 0.1, 10.0, false },
};
static const UZUVfxNumKey _ring_nums[] = {
	{ "radius", 3.0, 0.2, 50.0, false },
	{ "duration", 0.45, 0.05, 5.0, false },
};
static const UZUVfxNumKey _beam_nums[] = {
	{ "radius", 0.06, 0.01, 1.0, false },
	{ "duration", 0.18, 0.03, 5.0, false },
	{ "length", 4.0, 0.1, 100.0, false },
};
static const UZUVfxNumKey _light_nums[] = {
	{ "energy", 4.0, 0.0, 64.0, false },
	{ "range", 6.0, 0.5, 64.0, false },
	{ "duration", 0.3, 0.03, 5.0, false },
};
static const UZUVfxNumKey _shake_nums[] = {
	{ "amount", 0.4, 0.0, 2.0, false },
};
static const UZUVfxNumKey _hitstop_nums[] = {
	{ "duration", 0.05, 0.0, 2.0, false },
	{ "scale", 0.0, 0.0, 1.0, false },
};
static const UZUVfxNumKey _kick_nums[] = {
	{ "amount", 0.5, 0.0, 2.0, false },
};
static const UZUVfxNumKey _sfx_nums[] = {
	{ "seed", 0, -1000000, 1000000, true },
};

static const UZUVfxPartSpec _part_specs[] = {
	{ "sparks", _sparks_nums, 4, true, Color(1, 0.75, 0.35), true, false },
	{ "smoke", _smoke_nums, 4, true, Color(0.3, 0.28, 0.26), true, false },
	{ "debris", _debris_nums, 4, true, Color(0.45, 0.35, 0.28), true, false },
	{ "ring", _ring_nums, 2, true, Color(1, 0.85, 0.5), false, false },
	{ "beam", _beam_nums, 3, true, Color(0.45, 0.9, 1), false, false },
	{ "light", _light_nums, 3, true, Color(1, 0.7, 0.35), true, false },
	{ "shake", _shake_nums, 1, false, Color(), false, false },
	{ "hitstop", _hitstop_nums, 2, false, Color(), false, false },
	{ "kick", _kick_nums, 1, false, Color(), false, false },
	{ "sfx", _sfx_nums, 1, false, Color(), false, true },
};

static const UZUVfxPartSpec *_find_part_spec(const String &p_type) {
	for (const UZUVfxPartSpec &spec : _part_specs) {
		if (p_type == spec.type) {
			return &spec;
		}
	}
	return nullptr;
}

// How long one normalized part occupies the effect (its delay is added by
// the caller).
static double _part_span(const Dictionary &p_part) {
	String type = p_part["type"];
	if (type == "sparks" || type == "smoke" || type == "debris") {
		return double(p_part["lifetime"]);
	}
	if (type == "ring" || type == "beam" || type == "light" || type == "hitstop") {
		return double(p_part["duration"]);
	}
	return 0.0; // shake, kick, sfx are instantaneous.
}

static bool _is_number(const Variant &p_value) {
	return p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
}

static Array _color_array(const Color &p_color) {
	Array a;
	a.push_back(p_color.r);
	a.push_back(p_color.g);
	a.push_back(p_color.b);
	a.push_back(p_color.a);
	return a;
}

Dictionary UZUVfx::validate_recipe(const Dictionary &p_recipe) {
	Dictionary result;
	Array errors;
	Array warnings;
	Array parts_out;
	String name = "unnamed";
	double duration = 0.0;

	Array keys = p_recipe.keys();
	for (int i = 0; i < keys.size(); i++) {
		String key = keys[i];
		if (key != "name" && key != "parts") {
			errors.push_back("unknown recipe key '" + key + "' (only name and parts)");
		}
	}
	if (p_recipe.has("name")) {
		if (p_recipe["name"].get_type() == Variant::STRING) {
			name = p_recipe["name"];
		} else {
			errors.push_back("recipe 'name' must be a string");
		}
	}
	if (!p_recipe.has("parts") || p_recipe["parts"].get_type() != Variant::ARRAY) {
		errors.push_back("recipe needs a 'parts' array");
	} else {
		Array parts = p_recipe["parts"];
		if (parts.is_empty()) {
			errors.push_back("recipe 'parts' is empty");
		}
		for (int i = 0; i < parts.size(); i++) {
			String where = "part " + itos(i);
			if (parts[i].get_type() != Variant::DICTIONARY) {
				errors.push_back(where + " is not a dictionary");
				continue;
			}
			Dictionary part = parts[i];
			if (!part.has("type") || part["type"].get_type() != Variant::STRING) {
				errors.push_back(where + " needs a string 'type'");
				continue;
			}
			String type = part["type"];
			const UZUVfxPartSpec *spec = _find_part_spec(type);
			if (!spec) {
				errors.push_back(where + " has unknown type '" + type + "'");
				continue;
			}
			where += " (" + type + ")";
			Dictionary out;
			out["type"] = type;

			// Reject unknown keys so a typo is loud, not silent.
			Array part_keys = part.keys();
			for (int k = 0; k < part_keys.size(); k++) {
				String key = part_keys[k];
				if (key == "type" || key == "delay") {
					continue;
				}
				bool known = false;
				for (int n = 0; n < spec->num_count; n++) {
					known = known || key == spec->nums[n].name;
				}
				known = known || (key == "color" && spec->has_color);
				known = known || (key == "at" && spec->has_at);
				known = known || (key == "preset" && spec->has_preset);
				known = known || (key == "direction" && type == "beam");
				if (!known) {
					errors.push_back(where + " has unknown key '" + key + "'");
				}
			}

			double delay = 0.0;
			if (part.has("delay")) {
				if (_is_number(part["delay"])) {
					delay = double(part["delay"]);
					if (delay < 0.0 || delay > 10.0) {
						warnings.push_back(where + ": delay " + String::num(delay) + " clamped to 0..10");
						delay = CLAMP(delay, 0.0, 10.0);
					}
				} else {
					errors.push_back(where + ": 'delay' must be a number");
				}
			}
			out["delay"] = delay;

			for (int n = 0; n < spec->num_count; n++) {
				const UZUVfxNumKey &nk = spec->nums[n];
				double value = nk.def;
				if (part.has(nk.name)) {
					if (_is_number(part[nk.name])) {
						value = double(part[nk.name]);
						if (value < nk.min || value > nk.max) {
							warnings.push_back(where + ": " + nk.name + " " + String::num(value) + " clamped to " + String::num(nk.min) + ".." + String::num(nk.max));
							value = CLAMP(value, nk.min, nk.max);
						}
					} else {
						errors.push_back(where + ": '" + String(nk.name) + "' must be a number");
					}
				}
				if (nk.is_int) {
					out[nk.name] = int(Math::round(value));
				} else {
					out[nk.name] = value;
				}
			}

			if (spec->has_color) {
				Color color = spec->color_def;
				if (part.has("color")) {
					Variant given = part["color"];
					if (given.get_type() == Variant::COLOR) {
						color = given;
					} else if (given.get_type() == Variant::ARRAY) {
						Array comps = given;
						if (comps.size() == 3 || comps.size() == 4) {
							bool numeric = true;
							for (int c = 0; c < comps.size(); c++) {
								numeric = numeric && _is_number(comps[c]);
							}
							if (numeric) {
								color = Color(double(comps[0]), double(comps[1]), double(comps[2]), comps.size() == 4 ? double(comps[3]) : 1.0);
							} else {
								errors.push_back(where + ": 'color' components must be numbers");
							}
						} else {
							errors.push_back(where + ": 'color' needs 3 or 4 components");
						}
					} else {
						errors.push_back(where + ": 'color' must be an array [r, g, b] or a Color");
					}
				}
				if (color.r < 0.0 || color.r > 1.0 || color.g < 0.0 || color.g > 1.0 || color.b < 0.0 || color.b > 1.0 || color.a < 0.0 || color.a > 1.0) {
					warnings.push_back(where + ": color components clamped to 0..1");
					color = Color(CLAMP(color.r, 0, 1), CLAMP(color.g, 0, 1), CLAMP(color.b, 0, 1), CLAMP(color.a, 0, 1));
				}
				out["color"] = _color_array(color);
			}

			if (spec->has_at) {
				String at = "origin";
				if (part.has("at")) {
					if (part["at"].get_type() == Variant::STRING) {
						at = part["at"];
						if (at != "origin" && at != "target") {
							errors.push_back(where + ": 'at' must be \"origin\" or \"target\"");
						}
					} else {
						errors.push_back(where + ": 'at' must be a string");
					}
				}
				out["at"] = at;
			}

			if (type == "beam") {
				Array direction;
				direction.push_back(0.0);
				direction.push_back(0.0);
				direction.push_back(-1.0);
				if (part.has("direction")) {
					if (part["direction"].get_type() == Variant::ARRAY) {
						Array given = part["direction"];
						if (given.size() == 3 && _is_number(given[0]) && _is_number(given[1]) && _is_number(given[2])) {
							direction[0] = double(given[0]);
							direction[1] = double(given[1]);
							direction[2] = double(given[2]);
							if (Vector3(double(direction[0]), double(direction[1]), double(direction[2])).length() < 0.001) {
								errors.push_back(where + ": 'direction' is (nearly) zero");
							}
						} else {
							errors.push_back(where + ": 'direction' needs 3 numbers");
						}
					} else {
						errors.push_back(where + ": 'direction' must be an array [x, y, z]");
					}
				}
				out["direction"] = direction;
			}

			if (spec->has_preset) {
				String preset;
				if (part.has("preset")) {
					if (part["preset"].get_type() == Variant::STRING) {
						preset = part["preset"];
						if (!UZUSfx::get_preset_names().has(preset)) {
							errors.push_back(where + ": unknown sfx preset '" + preset + "'");
						}
					} else {
						errors.push_back(where + ": 'preset' must be a string");
					}
				} else {
					errors.push_back(where + " needs a 'preset'");
				}
				out["preset"] = preset;
			}

			duration = MAX(duration, delay + _part_span(out));
			parts_out.push_back(out);
		}
	}

	result["ok"] = errors.is_empty();
	result["name"] = name;
	result["errors"] = errors;
	result["warnings"] = warnings;
	result["parts"] = parts_out;
	// A small tail so the last particles are seen before the effect frees.
	result["duration"] = errors.is_empty() ? duration + 0.25 : 0.0;
	return result;
}

// ---------------------------------------------------------------------------
// The built-in sample recipes.

Dictionary UZUVfx::get_recipe(const String &p_name) {
	Dictionary recipe;
	Array parts;
	if (p_name == "explosion") {
		recipe["name"] = "explosion";
		Dictionary debris;
		debris["type"] = "debris";
		debris["count"] = 14;
		debris["power"] = 1.5;
		parts.push_back(debris);
		Dictionary sparks;
		sparks["type"] = "sparks";
		sparks["count"] = 42;
		sparks["power"] = 1.7;
		parts.push_back(sparks);
		Dictionary smoke;
		smoke["type"] = "smoke";
		smoke["count"] = 14;
		smoke["delay"] = 0.08;
		parts.push_back(smoke);
		Dictionary ring;
		ring["type"] = "ring";
		ring["radius"] = 3.2;
		parts.push_back(ring);
		Dictionary light;
		light["type"] = "light";
		light["energy"] = 6.0;
		light["duration"] = 0.35;
		parts.push_back(light);
		Dictionary shake;
		shake["type"] = "shake";
		shake["amount"] = 0.55;
		parts.push_back(shake);
		Dictionary hitstop;
		hitstop["type"] = "hitstop";
		hitstop["duration"] = 0.05;
		parts.push_back(hitstop);
		Dictionary sfx;
		sfx["type"] = "sfx";
		sfx["preset"] = "explosion";
		parts.push_back(sfx);
	} else if (p_name == "laser") {
		recipe["name"] = "laser";
		Dictionary beam;
		beam["type"] = "beam";
		parts.push_back(beam);
		Dictionary sparks;
		sparks["type"] = "sparks";
		sparks["count"] = 18;
		sparks["color"] = _color_array(Color(0.6, 0.95, 1.0));
		sparks["power"] = 1.0;
		sparks["lifetime"] = 0.35;
		sparks["at"] = "target";
		parts.push_back(sparks);
		Dictionary light;
		light["type"] = "light";
		light["color"] = _color_array(Color(0.4, 0.8, 1.0));
		light["energy"] = 3.0;
		light["range"] = 4.0;
		light["duration"] = 0.2;
		light["at"] = "target";
		parts.push_back(light);
		Dictionary sfx;
		sfx["type"] = "sfx";
		sfx["preset"] = "laser";
		parts.push_back(sfx);
	} else {
		return Dictionary();
	}
	recipe["parts"] = parts;
	return recipe;
}

PackedStringArray UZUVfx::get_recipe_names() {
	PackedStringArray names;
	names.push_back("explosion");
	names.push_back("laser");
	return names;
}

Dictionary UZUVfx::describe_recipe(const String &p_name) {
	Dictionary recipe = get_recipe(p_name);
	if (recipe.is_empty()) {
		Dictionary out;
		out["ok"] = false;
		out["error"] = "unknown recipe '" + p_name + "' (see vfx-recipes)";
		return out;
	}
	return validate_recipe(recipe);
}

// ---------------------------------------------------------------------------
// Live effects. An instance is a Node3D under the caller's parent; the parts
// that need per-frame work (delayed starts, ring / beam / light animation,
// delayed juice and sound) are tracked here as pure age values.

struct UZUVfxActivePart {
	Dictionary part; // The normalized part.
	ObjectID node_id; // The node to animate (ring, beam, light) or start (particles).
	Ref<StandardMaterial3D> material; // For alpha fades (ring, beam).
	bool started = false;
	double start_energy = 0.0; // Light parts.
};

struct UZUVfxActive {
	ObjectID root_id;
	Node *parent = nullptr; // For delayed sfx playback (kept raw; checked via root).
	String name;
	double age = 0.0;
	double duration = 0.0;
	Vector<UZUVfxActivePart> parts;
};

static Vector<UZUVfxActive> _active_effects;

static uint64_t _now_msec() {
	Time *time = Time::get_singleton();
	return time ? time->get_ticks_msec() : 0;
}

// The per-frame driver: created once by UZUVfx::ensure_runtime() as a child
// of the scene tree root. It processes always, like UZUJuice's runtime,
// because effect ages are wall-clock based.
class UZUVfxRuntime : public Node {
	GDCLASS(UZUVfxRuntime, Node);

public:
	UZUVfxRuntime() {
		set_process_mode(Node::PROCESS_MODE_ALWAYS);
		set_process(true);
		set_name("UZUVfxRuntime");
	}
	uint64_t last_msec = 0;
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_PROCESS) {
			uint64_t now = _now_msec();
			double dt = last_msec ? double(now - last_msec) / 1000.0 : 0.0;
			last_msec = now;
			UZUVfx::apply_effects(dt);
		}
	}
};

void UZUVfx::ensure_runtime() {
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Window *root = tree->get_root();
	if (!root) {
		return;
	}
	if (root->get_node_or_null(NodePath("UZUVfxRuntime"))) {
		return;
	}
	root->add_child(memnew(UZUVfxRuntime));
}

// Built fresh for each smoke emitter on purpose: a function-static Ref
// would be destroyed at process exit, after the RenderingServer is gone
// (the same trap UZUJuice's burst texture avoids).
static Ref<ImageTexture> _soft_dot_texture(int p_size, double p_softness) {
	Ref<Image> img = Image::create_empty(p_size, p_size, false, Image::FORMAT_RGBA8);
	img->fill(Color(0, 0, 0, 0));
	double half = p_size / 2.0;
	for (int y = 0; y < p_size; y++) {
		for (int x = 0; x < p_size; x++) {
			double d = Vector2(x - half + 0.5, y - half + 0.5).length() / half;
			double a = CLAMP((1.0 - d) / MAX(p_softness, 0.01), 0.0, 1.0);
			img->set_pixel(x, y, Color(1, 1, 1, a * a));
		}
	}
	return ImageTexture::create_from_image(img);
}

static Color _part_color(const Dictionary &p_part) {
	Array c = p_part["color"];
	return Color(double(c[0]), double(c[1]), double(c[2]), double(c[3]));
}

static Ref<StandardMaterial3D> _glow_material(const Color &p_color, bool p_additive) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_blend_mode(p_additive ? BaseMaterial3D::BLEND_MODE_ADD : BaseMaterial3D::BLEND_MODE_MIX);
	if (p_additive || p_color.a < 1.0) {
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	}
	mat->set_albedo(p_color);
	return mat;
}

static CPUParticles3D *_make_particles(const Dictionary &p_part, const Vector3 &p_local_pos, bool p_start_now) {
	CPUParticles3D *particles = memnew(CPUParticles3D);
	particles->set_amount(int(p_part["count"]));
	particles->set_lifetime(double(p_part["lifetime"]));
	particles->set_one_shot(true);
	particles->set_use_local_coordinates(false);
	particles->set_position(p_local_pos);
	particles->set_emitting(p_start_now);
	return particles;
}

static Dictionary _fire_impl(Node *p_parent, const Vector3 &p_origin, const Vector3 &p_target, bool p_has_target, const Dictionary &p_recipe) {
	Dictionary checked = UZUVfx::validate_recipe(p_recipe);
	if (!bool(checked["ok"])) {
		Dictionary out;
		out["ok"] = false;
		out["name"] = checked["name"];
		out["errors"] = checked["errors"];
		out["warnings"] = checked["warnings"];
		return out;
	}
	Array parts = checked["parts"];
	String name = checked["name"];
	Vector3 local_target = p_has_target ? p_target - p_origin : Vector3();

	Node3D *root = memnew(Node3D);
	root->set_name("UZUVfx_" + name);
	p_parent->add_child(root);
	if (Node3D *parent3d = Object::cast_to<Node3D>(p_parent)) {
		if (parent3d->is_inside_tree()) {
			root->set_global_position(p_origin);
		} else {
			root->set_position(p_origin);
		}
	} else {
		root->set_position(p_origin);
	}

	UZUVfxActive active;
	active.root_id = root->get_instance_id();
	active.parent = p_parent;
	active.name = name;
	active.duration = double(checked["duration"]);

	Array part_types;
	Array skipped;
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		String type = part["type"];
		part_types.push_back(type);
		double delay = double(part["delay"]);
		bool start_now = delay <= 0.0;
		Vector3 at_pos = (part.get("at", "origin") == String("target") && p_has_target) ? local_target : Vector3();
		UZUVfxActivePart ap;
		ap.part = part;

		if (type == "sparks") {
			double power = double(part["power"]);
			CPUParticles3D *particles = _make_particles(part, at_pos, start_now);
			particles->set_explosiveness_ratio(0.95);
			particles->set_direction(Vector3(0, 1, 0));
			particles->set_spread(180.0);
			particles->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 3.0 * power);
			particles->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 6.0 * power);
			particles->set_gravity(Vector3(0, -3, 0));
			particles->set_param_min(CPUParticles3D::PARAM_DAMPING, 1.0);
			particles->set_param_max(CPUParticles3D::PARAM_DAMPING, 2.0);
			Ref<QuadMesh> quad;
			quad.instantiate();
			double size = double(part["size"]);
			quad->set_size(Vector2(size, size));
			quad->set_material(_glow_material(_part_color(part), true));
			particles->set_mesh(quad);
			particles->set_name("Sparks");
			root->add_child(particles);
			ap.node_id = particles->get_instance_id();
			ap.started = start_now;
		} else if (type == "smoke") {
			double rise = double(part["rise"]);
			CPUParticles3D *particles = _make_particles(part, at_pos, start_now);
			particles->set_explosiveness_ratio(0.55);
			particles->set_direction(Vector3(0, 1, 0));
			particles->set_spread(35.0);
			particles->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 0.5 * rise);
			particles->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 1.0 * rise);
			particles->set_gravity(Vector3(0, 0, 0));
			particles->set_param_min(CPUParticles3D::PARAM_DAMPING, 0.4);
			particles->set_param_max(CPUParticles3D::PARAM_DAMPING, 0.8);
			Ref<Curve> growth;
			growth.instantiate();
			growth->clear_points();
			growth->add_point(Vector2(0, 0.5));
			growth->add_point(Vector2(1, 1.9));
			particles->set_param_curve(CPUParticles3D::PARAM_SCALE, growth);
			Color tint = _part_color(part);
			Ref<Gradient> ramp;
			ramp.instantiate();
			ramp->set_color(0, Color(tint.r, tint.g, tint.b, 0.32));
			ramp->set_color(1, Color(tint.r, tint.g, tint.b, 0.0));
			particles->set_color_ramp(ramp);
			Ref<StandardMaterial3D> mat;
			mat.instantiate();
			mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
			mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			mat->set_albedo(Color(1, 1, 1, 1));
			mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, _soft_dot_texture(32, 0.9));
			Ref<QuadMesh> quad;
			quad.instantiate();
			double size = double(part["size"]);
			quad->set_size(Vector2(size, size));
			quad->set_material(mat);
			particles->set_mesh(quad);
			particles->set_name("Smoke");
			root->add_child(particles);
			ap.node_id = particles->get_instance_id();
			ap.started = start_now;
		} else if (type == "debris") {
			double power = double(part["power"]);
			CPUParticles3D *particles = _make_particles(part, at_pos, start_now);
			particles->set_explosiveness_ratio(0.95);
			particles->set_direction(Vector3(0, 1, 0));
			particles->set_spread(60.0);
			particles->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 2.5 * power);
			particles->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 5.0 * power);
			particles->set_gravity(Vector3(0, -9.8, 0));
			particles->set_param_min(CPUParticles3D::PARAM_DAMPING, 0.1);
			particles->set_param_max(CPUParticles3D::PARAM_DAMPING, 0.3);
			particles->set_param_min(CPUParticles3D::PARAM_ANGLE, -180.0);
			particles->set_param_max(CPUParticles3D::PARAM_ANGLE, 180.0);
			Ref<StandardMaterial3D> mat;
			mat.instantiate();
			mat->set_albedo(_part_color(part));
			Ref<BoxMesh> box;
			box.instantiate();
			double size = double(part["size"]);
			box->set_size(Vector3(size, size, size));
			box->set_material(mat);
			particles->set_mesh(box);
			particles->set_name("Debris");
			root->add_child(particles);
			ap.node_id = particles->get_instance_id();
			ap.started = start_now;
		} else if (type == "ring") {
			Ref<TorusMesh> torus;
			torus.instantiate();
			torus->set_inner_radius(0.86);
			torus->set_outer_radius(1.0);
			torus->set_rings(48);
			torus->set_ring_segments(8);
			Ref<StandardMaterial3D> mat = _glow_material(_part_color(part), true);
			torus->set_material(mat);
			MeshInstance3D *ring = memnew(MeshInstance3D);
			ring->set_mesh(torus);
			ring->set_name("Ring");
			ring->set_position(Vector3(0, 0.05, 0));
			ring->set_scale(Vector3(0.001, 1, 0.001));
			ring->set_visible(start_now);
			root->add_child(ring);
			ap.node_id = ring->get_instance_id();
			ap.material = mat;
			ap.started = start_now;
		} else if (type == "beam") {
			Vector3 dir_vec;
			if (p_has_target) {
				dir_vec = local_target;
			} else {
				Array d = part["direction"];
				dir_vec = Vector3(double(d[0]), double(d[1]), double(d[2])).normalized() * double(part["length"]);
			}
			double len = MAX(dir_vec.length(), 0.01);
			Vector3 dir = dir_vec / len;
			Ref<CylinderMesh> cylinder;
			cylinder.instantiate();
			double radius = double(part["radius"]);
			cylinder->set_top_radius(radius);
			cylinder->set_bottom_radius(radius);
			cylinder->set_height(1.0);
			cylinder->set_radial_segments(12);
			Ref<StandardMaterial3D> mat = _glow_material(_part_color(part), true);
			cylinder->set_material(mat);
			MeshInstance3D *beam = memnew(MeshInstance3D);
			beam->set_mesh(cylinder);
			beam->set_name("Beam");
			Quaternion quat(Vector3(0, 1, 0), dir);
			Basis basis = Basis(quat) * Basis::from_scale(Vector3(1, len, 1));
			beam->set_transform(Transform3D(basis, dir_vec * 0.5));
			beam->set_visible(start_now);
			root->add_child(beam);
			ap.node_id = beam->get_instance_id();
			ap.material = mat;
			ap.started = start_now;
		} else if (type == "light") {
			OmniLight3D *light = memnew(OmniLight3D);
			light->set_name("Flash");
			light->set_color(_part_color(part));
			light->set_param(Light3D::PARAM_RANGE, double(part["range"]));
			light->set_param(Light3D::PARAM_ENERGY, start_now ? double(part["energy"]) : 0.0);
			light->set_position(at_pos + Vector3(0, 0.2, 0));
			root->add_child(light);
			ap.node_id = light->get_instance_id();
			ap.start_energy = double(part["energy"]);
			ap.started = start_now;
		} else {
			// shake / hitstop / kick / sfx: fired by apply_effects when
			// their delay comes (at once for a zero delay, through the
			// age >= delay check in the first apply_effects call).
			ap.started = false;
			if (type == "sfx" && !AudioServer::get_singleton()) {
				skipped.push_back("sfx (no AudioServer)");
				ap.started = true; // Nothing to wait for.
			}
		}
		active.parts.push_back(ap);
	}
	_active_effects.push_back(active);
	UZUVfx::ensure_runtime();

	Dictionary out;
	out["ok"] = true;
	out["name"] = name;
	out["parts"] = parts;
	out["part_types"] = part_types;
	out["duration"] = active.duration;
	out["warnings"] = checked["warnings"];
	if (!skipped.is_empty()) {
		out["skipped"] = skipped;
	}
	out["effect"] = root;
	return out;
}

Dictionary UZUVfx::fire(Node *p_parent, const Vector3 &p_origin, const Dictionary &p_recipe) {
	ERR_FAIL_NULL_V(p_parent, Dictionary());
	return _fire_impl(p_parent, p_origin, Vector3(), false, p_recipe);
}

Dictionary UZUVfx::fire_between(Node *p_parent, const Vector3 &p_origin, const Vector3 &p_target, const Dictionary &p_recipe) {
	ERR_FAIL_NULL_V(p_parent, Dictionary());
	return _fire_impl(p_parent, p_origin, p_target, true, p_recipe);
}

Dictionary UZUVfx::fire_named(Node *p_parent, const Vector3 &p_origin, const String &p_name) {
	ERR_FAIL_NULL_V(p_parent, Dictionary());
	Dictionary recipe = get_recipe(p_name);
	if (recipe.is_empty()) {
		Dictionary out;
		out["ok"] = false;
		out["error"] = "unknown recipe '" + p_name + "' (see vfx-recipes)";
		return out;
	}
	return _fire_impl(p_parent, p_origin, Vector3(), false, recipe);
}

Dictionary UZUVfx::fire_named_between(Node *p_parent, const Vector3 &p_origin, const Vector3 &p_target, const String &p_name) {
	ERR_FAIL_NULL_V(p_parent, Dictionary());
	Dictionary recipe = get_recipe(p_name);
	if (recipe.is_empty()) {
		Dictionary out;
		out["ok"] = false;
		out["error"] = "unknown recipe '" + p_name + "' (see vfx-recipes)";
		return out;
	}
	return _fire_impl(p_parent, p_origin, p_target, true, recipe);
}

int UZUVfx::get_active_count() {
	return _active_effects.size();
}

Array UZUVfx::get_active_effects() {
	Array out;
	for (const UZUVfxActive &active : _active_effects) {
		Dictionary d;
		d["name"] = active.name;
		d["age"] = active.age;
		d["duration"] = active.duration;
		d["remaining"] = MAX(active.duration - active.age, 0.0);
		Array types;
		for (const UZUVfxActivePart &ap : active.parts) {
			types.push_back(String(ap.part["type"]));
		}
		d["parts"] = types;
		out.push_back(d);
	}
	return out;
}

static void _free_effect_root(ObjectID p_root_id) {
	Object *obj = ObjectDB::get_instance(p_root_id);
	if (Node *node = Object::cast_to<Node>(obj)) {
		if (node->is_inside_tree()) {
			node->queue_free();
		} else {
			memdelete(node);
		}
	}
}

void UZUVfx::clear_effects() {
	for (const UZUVfxActive &active : _active_effects) {
		_free_effect_root(active.root_id);
	}
	_active_effects.clear();
}

void UZUVfx::apply_effects(double p_real_dt) {
	double dt = MAX(p_real_dt, 0.0);
	for (int i = _active_effects.size() - 1; i >= 0; i--) {
		UZUVfxActive &active = _active_effects.write[i];
		Object *root_obj = ObjectDB::get_instance(active.root_id);
		if (!root_obj) {
			_active_effects.remove_at(i);
			continue;
		}
		active.age += dt;
		for (UZUVfxActivePart &ap : active.parts) {
			Dictionary part = ap.part;
			String type = part["type"];
			double delay = double(part["delay"]);
			if (!ap.started) {
				if (active.age < delay) {
					continue;
				}
				ap.started = true;
				if (type == "shake") {
					UZUJuice::shake(double(part["amount"]));
				} else if (type == "hitstop") {
					UZUJuice::hitstop(double(part["duration"]), double(part["scale"]));
				} else if (type == "kick") {
					UZUJuice::kick(double(part["amount"]));
				} else if (type == "sfx") {
					if (AudioServer::get_singleton()) {
						Ref<AudioStreamWAV> stream = UZUSfx::generate_preset(String(part["preset"]), int(part["seed"]));
						if (stream.is_valid()) {
							Node *host = Object::cast_to<Node>(root_obj);
							UZUSfx::play(host, stream);
						}
					}
				} else if (Object *node_obj = ObjectDB::get_instance(ap.node_id)) {
					if (CPUParticles3D *particles = Object::cast_to<CPUParticles3D>(node_obj)) {
						particles->set_emitting(true);
					} else if (Node3D *node3d = Object::cast_to<Node3D>(node_obj)) {
						node3d->set_visible(true);
					}
					if (OmniLight3D *light = Object::cast_to<OmniLight3D>(node_obj)) {
						light->set_param(Light3D::PARAM_ENERGY, ap.start_energy);
					}
				}
			}
			// Per-frame animation of the spatial parts.
			double span = _part_span(part);
			if (span <= 0.0 || !ap.started) {
				continue;
			}
			double progress = CLAMP((active.age - delay) / span, 0.0, 1.0);
			if (type == "ring") {
				if (Node3D *node3d = Object::cast_to<Node3D>(ObjectDB::get_instance(ap.node_id))) {
					double radius = double(part["radius"]);
					double eased = 1.0 - Math::pow(1.0 - progress, 2.0);
					double scale = MAX(radius * (0.1 + 0.9 * eased), 0.001);
					node3d->set_scale(Vector3(scale, 1, scale));
				}
				if (ap.material.is_valid()) {
					Color c = ap.material->get_albedo();
					Array base = part["color"];
					c.a = float(double(base[3]) * (1.0 - progress));
					ap.material->set_albedo(c);
				}
			} else if (type == "beam") {
				if (ap.material.is_valid()) {
					Color c = ap.material->get_albedo();
					Array base = part["color"];
					c.a = float(double(base[3]) * (1.0 - progress));
					ap.material->set_albedo(c);
				}
			} else if (type == "light") {
				if (OmniLight3D *light = Object::cast_to<OmniLight3D>(ObjectDB::get_instance(ap.node_id))) {
					light->set_param(Light3D::PARAM_ENERGY, ap.start_energy * (1.0 - progress));
				}
			}
		}
		if (active.age >= active.duration) {
			_free_effect_root(active.root_id);
			_active_effects.remove_at(i);
		}
	}
}

void UZUVfx::_bind_methods() {
	ClassDB::bind_static_method("UZUVfx", D_METHOD("get_recipe_names"), &UZUVfx::get_recipe_names);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("get_recipe", "name"), &UZUVfx::get_recipe);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("describe_recipe", "name"), &UZUVfx::describe_recipe);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("validate_recipe", "recipe"), &UZUVfx::validate_recipe);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("fire", "parent", "origin", "recipe"), &UZUVfx::fire);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("fire_between", "parent", "origin", "target", "recipe"), &UZUVfx::fire_between);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("fire_named", "parent", "origin", "name"), &UZUVfx::fire_named);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("fire_named_between", "parent", "origin", "target", "name"), &UZUVfx::fire_named_between);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("get_active_count"), &UZUVfx::get_active_count);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("get_active_effects"), &UZUVfx::get_active_effects);
	ClassDB::bind_static_method("UZUVfx", D_METHOD("clear_effects"), &UZUVfx::clear_effects);
}
