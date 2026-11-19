/**************************************************************************/
/*  test_uzu_vfx.cpp                                                      */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_uzu_vfx)

#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/uzu_juice.h"
#include "scene/3d/uzu_vfx.h"

namespace TestUZUVfx {

static Array _part_types(const Dictionary &p_checked) {
	Array types;
	Array parts = p_checked["parts"];
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		types.push_back(String(part["type"]));
	}
	return types;
}

TEST_CASE("[Scene][UZUVfx] The explosion recipe validates with its parts in order") {
	Dictionary checked = UZUVfx::describe_recipe("explosion");
	REQUIRE(bool(checked["ok"]));
	CHECK(String(checked["name"]) == "explosion");
	Array types = _part_types(checked);
	REQUIRE(types.size() == 8);
	CHECK(types == Array({ "debris", "sparks", "smoke", "ring", "light", "shake", "hitstop", "sfx" }));
	// The smoke ends last: delay 0.08 + lifetime 1.6, plus the particle tail.
	CHECK(double(checked["duration"]) == doctest::Approx(0.08 + 1.6 + 0.25));
	Array parts = checked["parts"];
	Dictionary sparks = parts[1];
	CHECK(int(sparks["count"]) == 42); // As declared in the recipe.
	Dictionary smoke = parts[2];
	Array smoke_color = smoke["color"];
	CHECK(double(smoke_color[0]) == doctest::Approx(0.3)); // The default, filled in.
	CHECK(String(smoke["at"]) == "origin");
	Dictionary sfx = parts[7];
	CHECK(String(sfx["preset"]) == "explosion");
}

TEST_CASE("[Scene][UZUVfx] The laser recipe validates; the beam defaults to forward") {
	Dictionary checked = UZUVfx::describe_recipe("laser");
	REQUIRE(bool(checked["ok"]));
	Array types = _part_types(checked);
	CHECK(types == Array({ "beam", "sparks", "light", "sfx" }));
	Array parts = checked["parts"];
	Dictionary beam = parts[0];
	Array direction = beam["direction"];
	CHECK(double(direction[0]) == doctest::Approx(0.0));
	CHECK(double(direction[2]) == doctest::Approx(-1.0));
	CHECK(double(beam["length"]) == doctest::Approx(4.0));
	Dictionary sparks = parts[1];
	CHECK(String(sparks["at"]) == "target"); // Impact sparks at the far end.
	Dictionary unknown = UZUVfx::describe_recipe("fireworks");
	CHECK_FALSE(bool(unknown["ok"]));
}

TEST_CASE("[Scene][UZUVfx] Validation fills defaults and clamps with warnings") {
	Dictionary recipe;
	Array parts;
	Dictionary ring;
	ring["type"] = "ring";
	parts.push_back(ring);
	Dictionary sparks;
	sparks["type"] = "sparks";
	sparks["count"] = 99999;
	parts.push_back(sparks);
	recipe["parts"] = parts;
	Dictionary checked = UZUVfx::validate_recipe(recipe);
	REQUIRE(bool(checked["ok"]));
	Array out_parts = checked["parts"];
	Dictionary ring_out = out_parts[0];
	CHECK(double(ring_out["radius"]) == doctest::Approx(3.0)); // Default.
	CHECK(double(ring_out["delay"]) == doctest::Approx(0.0));
	Dictionary sparks_out = out_parts[1];
	CHECK(int(sparks_out["count"]) == 512); // Clamped to the range.
	Array warnings = checked["warnings"];
	REQUIRE(warnings.size() == 1);
	CHECK(String(warnings[0]).contains("count"));
	CHECK(double(checked["duration"]) == doctest::Approx(0.45 + 0.25)); // The ring outlasts the sparks.
}

TEST_CASE("[Scene][UZUVfx] Validation rejects broken recipes") {
	Dictionary no_parts;
	CHECK_FALSE(bool(UZUVfx::validate_recipe(no_parts)["ok"]));

	Dictionary recipe;
	Array parts;
	Dictionary bad_type;
	bad_type["type"] = "shockwave";
	parts.push_back(bad_type);
	recipe["parts"] = parts;
	Dictionary checked = UZUVfx::validate_recipe(recipe);
	CHECK_FALSE(bool(checked["ok"]));
	CHECK(String(Array(checked["errors"])[0]).contains("shockwave"));

	Dictionary recipe2;
	Array parts2;
	Dictionary bad_key;
	bad_key["type"] = "ring";
	bad_key["raduis"] = 3.0; // A typo must be loud.
	parts2.push_back(bad_key);
	recipe2["parts"] = parts2;
	CHECK_FALSE(bool(UZUVfx::validate_recipe(recipe2)["ok"]));

	Dictionary recipe3;
	Array parts3;
	Dictionary bad_sfx;
	bad_sfx["type"] = "sfx";
	bad_sfx["preset"] = "boom";
	parts3.push_back(bad_sfx);
	recipe3["parts"] = parts3;
	CHECK_FALSE(bool(UZUVfx::validate_recipe(recipe3)["ok"]));

	Dictionary recipe4;
	Array parts4;
	Dictionary bad_color;
	bad_color["type"] = "ring";
	Array two;
	two.push_back(1.0);
	two.push_back(0.5);
	bad_color["color"] = two;
	parts4.push_back(bad_color);
	recipe4["parts"] = parts4;
	CHECK_FALSE(bool(UZUVfx::validate_recipe(recipe4)["ok"]));
}

TEST_CASE("[Scene][UZUVfx] Firing builds the part nodes, animates and frees them") {
	UZUVfx::clear_effects();
	UZUJuice::clear_shake(); // Trauma is shared state; other suites leave some behind.
	Node3D *parent = memnew(Node3D);
	Dictionary recipe;
	recipe["name"] = "test_pop";
	Array parts;
	Dictionary sparks;
	sparks["type"] = "sparks";
	sparks["count"] = 10;
	parts.push_back(sparks);
	Dictionary ring;
	ring["type"] = "ring";
	ring["duration"] = 0.4;
	parts.push_back(ring);
	Dictionary light;
	light["type"] = "light";
	light["energy"] = 4.0;
	light["duration"] = 0.4;
	parts.push_back(light);
	Dictionary shake;
	shake["type"] = "shake";
	shake["amount"] = 0.3;
	parts.push_back(shake);
	recipe["parts"] = parts;

	Dictionary fired = UZUVfx::fire(parent, Vector3(1, 2, 3), recipe);
	REQUIRE(bool(fired["ok"]));
	CHECK(UZUVfx::get_active_count() == 1);
	Object *effect_obj = fired["effect"];
	Node *root = Object::cast_to<Node>(effect_obj);
	REQUIRE(root != nullptr);
	CHECK(root->get_parent() == parent);
	CPUParticles3D *sparks_node = Object::cast_to<CPUParticles3D>(root->get_node_or_null(NodePath("Sparks")));
	REQUIRE(sparks_node != nullptr);
	CHECK(sparks_node->get_amount() == 10);
	CHECK(sparks_node->is_emitting());
	CHECK(root->get_node_or_null(NodePath("Ring")) != nullptr);
	OmniLight3D *light_node = Object::cast_to<OmniLight3D>(root->get_node_or_null(NodePath("Flash")));
	REQUIRE(light_node != nullptr);
	CHECK(light_node->get_param(Light3D::PARAM_ENERGY) == doctest::Approx(4.0));

	Array active = UZUVfx::get_active_effects();
	REQUIRE(active.size() == 1);
	Dictionary entry = active[0];
	CHECK(String(entry["name"]) == "test_pop");
	CHECK(Array(entry["parts"]) == Array({ "sparks", "ring", "light", "shake" }));

	UZUVfx::apply_effects(0.0); // Starts the instant parts (the shake).
	CHECK(UZUJuice::get_trauma() == doctest::Approx(0.3));
	UZUJuice::clear_shake();

	UZUVfx::apply_effects(0.2); // Halfway through the ring and the light.
	CHECK(light_node->get_param(Light3D::PARAM_ENERGY) == doctest::Approx(2.0));
	MeshInstance3D *ring_node = Object::cast_to<MeshInstance3D>(root->get_node_or_null(NodePath("Ring")));
	REQUIRE(ring_node != nullptr);
	CHECK(ring_node->get_scale().x > 1.0); // The ring has grown past its start.

	ObjectID root_id = root->get_instance_id();
	UZUVfx::apply_effects(10.0); // Well past the total duration (0.65).
	CHECK(UZUVfx::get_active_count() == 0);
	CHECK(ObjectDB::get_instance(root_id) == nullptr); // The effect freed itself.
	memdelete(parent);
}

TEST_CASE("[Scene][UZUVfx] A delayed part starts when its delay comes") {
	UZUVfx::clear_effects();
	Node3D *parent = memnew(Node3D);
	Dictionary recipe;
	Array parts;
	Dictionary smoke;
	smoke["type"] = "smoke";
	smoke["count"] = 5;
	smoke["delay"] = 0.5;
	smoke["lifetime"] = 0.5;
	parts.push_back(smoke);
	recipe["parts"] = parts;
	Dictionary fired = UZUVfx::fire(parent, Vector3(), recipe);
	REQUIRE(bool(fired["ok"]));
	Object *effect_obj = fired["effect"];
	Node *root = Object::cast_to<Node>(effect_obj);
	REQUIRE(root != nullptr);
	CPUParticles3D *smoke_node = Object::cast_to<CPUParticles3D>(root->get_node_or_null(NodePath("Smoke")));
	REQUIRE(smoke_node != nullptr);
	CHECK_FALSE(smoke_node->is_emitting());
	UZUVfx::apply_effects(0.3);
	CHECK_FALSE(smoke_node->is_emitting()); // Still waiting.
	UZUVfx::apply_effects(0.3); // Age 0.6: past the delay.
	CHECK(smoke_node->is_emitting());
	UZUVfx::clear_effects();
	memdelete(parent);
}

TEST_CASE("[Scene][UZUVfx] fire_between spans the beam from origin to target") {
	UZUVfx::clear_effects();
	Node3D *parent = memnew(Node3D);
	Dictionary recipe;
	Array parts;
	Dictionary beam;
	beam["type"] = "beam";
	beam["duration"] = 0.3;
	parts.push_back(beam);
	Dictionary sparks;
	sparks["type"] = "sparks";
	sparks["at"] = "target";
	parts.push_back(sparks);
	recipe["parts"] = parts;
	Dictionary fired = UZUVfx::fire_between(parent, Vector3(0, 0, 0), Vector3(4, 0, 0), recipe);
	REQUIRE(bool(fired["ok"]));
	Object *effect_obj = fired["effect"];
	Node *root = Object::cast_to<Node>(effect_obj);
	REQUIRE(root != nullptr);
	MeshInstance3D *beam_node = Object::cast_to<MeshInstance3D>(root->get_node_or_null(NodePath("Beam")));
	REQUIRE(beam_node != nullptr);
	Transform3D t = beam_node->get_transform();
	CHECK(t.origin == Vector3(2, 0, 0)); // The midpoint.
	CHECK(t.basis.get_column(1).length() == doctest::Approx(4.0)); // The cylinder's Y axis is the beam.
	CPUParticles3D *sparks_node = Object::cast_to<CPUParticles3D>(root->get_node_or_null(NodePath("Sparks")));
	REQUIRE(sparks_node != nullptr);
	CHECK(sparks_node->get_position() == Vector3(4, 0, 0)); // At the target.
	UZUVfx::clear_effects();
	memdelete(parent);
}

TEST_CASE("[Scene][UZUVfx] An invalid recipe fires nothing") {
	UZUVfx::clear_effects();
	Node3D *parent = memnew(Node3D);
	Dictionary recipe;
	Array parts;
	Dictionary bad;
	bad["type"] = "shockwave";
	parts.push_back(bad);
	recipe["parts"] = parts;
	Dictionary fired = UZUVfx::fire(parent, Vector3(), recipe);
	CHECK_FALSE(bool(fired["ok"]));
	CHECK(parent->get_child_count() == 0);
	CHECK(UZUVfx::get_active_count() == 0);
	Dictionary unknown = UZUVfx::fire_named(parent, Vector3(), "fireworks");
	CHECK_FALSE(bool(unknown["ok"]));
	memdelete(parent);
}

} // namespace TestUZUVfx

TEST_CASE("[Scene][Audio][UZUVfx] The explosion recipe fires whole, sound included") {
	UZUVfx::clear_effects();
	Node3D *parent = memnew(Node3D);
	Dictionary fired = UZUVfx::fire_named(parent, Vector3(0, 1, 0), "explosion");
	REQUIRE(bool(fired["ok"]));
	CHECK(Array(fired["part_types"]) == Array({ "debris", "sparks", "smoke", "ring", "light", "shake", "hitstop", "sfx" }));
	CHECK(UZUVfx::get_active_count() == 1);
	Object *effect_obj = fired["effect"];
	Node *root = Object::cast_to<Node>(effect_obj);
	REQUIRE(root != nullptr);
	CHECK(root->get_node_or_null(NodePath("Debris")) != nullptr);
	CHECK(root->get_node_or_null(NodePath("Smoke")) != nullptr);
	UZUVfx::apply_effects(0.0); // Starts the shake, hitstop and the sound.
	CHECK(UZUJuice::is_time_effect_active()); // The hitstop engaged.
	UZUJuice::cancel_time_effects();
	UZUJuice::clear_shake();
	UZUVfx::apply_effects(3.0); // Past the total duration (1.93).
	CHECK(UZUVfx::get_active_count() == 0);
	memdelete(parent);
}
