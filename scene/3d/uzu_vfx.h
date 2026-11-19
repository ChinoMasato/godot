/**************************************************************************/
/*  uzu_vfx.h                                                             */
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

#pragma once

#include "core/object/object.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class Node;

// U.ZU visual effects by recipe (BACKLOG B-1109): a composite effect -- an
// explosion, a laser -- is declared as data (a Dictionary, or JSON through
// the agent tools) instead of code: a list of parts with parameters. One
// call fires the whole recipe: the new visual parts (sparks, smoke, debris,
// a shockwave ring, a beam, a point-light flash) plus the UZUJuice parts
// (shake, hitstop, kick) and a synthesized UZUSfx sound, each with its own
// delay. This is the declarative generalisation of UZUJuice's hardcoded
// event presets (B-1106); it lives in its own class for the same reason
// UZUSfx does: UZUJuice works on nodes the game already has, while a recipe
// spawns a short-lived assembly of effect nodes and tracks it to the end of
// its life.
//
// A recipe is a Dictionary:
//   {"name": "explosion", "parts": [{"type": "sparks", "count": 40, ...}]}
// Part types and their parameters (every value has a default and a range;
// validate_recipe fills the defaults and reports what it clamped):
//   sparks  count 1..512 (24), color (1, .75, .35), power .1..8 (1),
//           lifetime .05..10 (.45), size .01..1 (.06), at origin|target
//   smoke   count 1..256 (12), color (.3, .28, .26), lifetime .1..20 (1.6),
//           size .1..4 (.8), rise 0..5 (.9), at
//   debris  count 1..256 (12), color (.45, .35, .28), power .1..8 (1.2),
//           size .02..0.5 (.07), lifetime .1..10 (.9), at
//   ring    color (1, .85, .5), radius .2..50 (3), duration .05..5 (.45)
//   beam    color (.45, .9, 1), radius .01..1 (.06), duration .03..5 (.18),
//           length .1..100 (4) and direction [0, 0, -1] when no target
//   light   color (1, .7, .35), energy 0..64 (4), range .5..64 (6),
//           duration .03..5 (.3), at
//   shake   amount 0..2 (.4)          (UZUJuice)
//   hitstop duration 0..2 (.05), scale 0..1 (0)
//   kick    amount 0..2 (.5)          (UZUJuice)
//   sfx     preset (a UZUSfx preset name), seed (0)
// Every part also takes "delay" (0..10 s, default 0). "flash" and "squash"
// are not recipe parts: they need a specific target node, which a positional
// recipe does not have; call UZUJuice for those. There is no destruction
// simulation: debris is a burst of small meshes on parabolic paths, which
// stands in for it (the B-1109 spec).
//
// Validation is a pure function of the recipe, so an agent (or a test) can
// check a recipe and read back the normalized parts without a scene tree.
// Fired effects are tracked as pure age/duration values and advanced by
// apply_effects(), which the runtime node calls every frame; tests can
// advance them by hand the same way.
class UZUVfx : public Object {
	GDCLASS(UZUVfx, Object);

protected:
	static void _bind_methods();

public:
	// The built-in sample recipes: "explosion" and "laser".
	static PackedStringArray get_recipe_names();
	static Dictionary get_recipe(const String &p_name);
	// A built-in recipe, validated and normalized (same shape as
	// validate_recipe's result), or {"ok": false, "error": ...}.
	static Dictionary describe_recipe(const String &p_name);

	// Checks a recipe and normalizes it: {"ok", "name", "errors",
	// "warnings", "parts" (normalized, defaults filled), "duration" (the
	// effect's total life in seconds)}. Pure: no scene tree, no singletons.
	static Dictionary validate_recipe(const Dictionary &p_recipe);

	// Fires a recipe under p_parent at p_origin and returns the validation
	// result plus "effect" (the root node) and "part_types". A beam spans
	// from p_origin toward p_target in fire_between; without a target it
	// uses its direction and length. An invalid recipe fires nothing and
	// returns ok = false with the errors.
	static Dictionary fire(Node *p_parent, const Vector3 &p_origin, const Dictionary &p_recipe);
	static Dictionary fire_between(Node *p_parent, const Vector3 &p_origin, const Vector3 &p_target, const Dictionary &p_recipe);
	static Dictionary fire_named(Node *p_parent, const Vector3 &p_origin, const String &p_name);
	static Dictionary fire_named_between(Node *p_parent, const Vector3 &p_origin, const Vector3 &p_target, const String &p_name);

	// The effects alive right now, as pure values: {"name", "age",
	// "duration", "remaining", "parts" (the part types)}.
	static int get_active_count();
	static Array get_active_effects();
	// Advances every live effect by p_real_dt seconds: starts delayed
	// parts, animates rings, beams and lights, frees finished effects.
	// Called by the runtime node each frame; tests call it directly.
	static void apply_effects(double p_real_dt);
	// Frees every live effect now (test cleanup).
	static void clear_effects();
	// Creates the runtime node in the scene tree if there is none yet.
	static void ensure_runtime();
};
