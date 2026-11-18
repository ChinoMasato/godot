/**************************************************************************/
/*  test_uzu_juice.cpp                                                    */
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

TEST_FORCE_LINK(test_uzu_juice)

#include "core/os/os.h"
#include "scene/2d/cpu_particles_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/uzu_juice.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/uzu_sfx.h"
#include <cstdio>

namespace TestUZUJuice {

TEST_CASE("[Scene][UZUJuice] Time state: idle is the base scale and inactive") {
	UZUJuiceTimeState state;
	CHECK(state.effective_scale(1000) == doctest::Approx(1.0));
	CHECK_FALSE(state.active(1000));
}

TEST_CASE("[Scene][UZUJuice] Time state: hitstop runs for its duration, then restores") {
	UZUJuiceTimeState state;
	state.start_hitstop(1000, 0.06, 0.0, 1.0);
	CHECK(state.active(1000));
	CHECK(state.effective_scale(1000) == doctest::Approx(0.0));
	CHECK(state.effective_scale(1059) == doctest::Approx(0.0));
	CHECK_FALSE(state.active(1060));
	CHECK(state.effective_scale(1060) == doctest::Approx(1.0));
}

TEST_CASE("[Scene][UZUJuice] Time state: hitstop wins over slow motion, slow motion resumes") {
	UZUJuiceTimeState state;
	state.start_slowmo(1000, 0.25, 0.4, 1.0);
	CHECK(state.effective_scale(1100) == doctest::Approx(0.25));
	state.start_hitstop(1100, 0.05, 0.0, 0.25);
	// The base is still the pre-effect scale, not the slow-motion scale.
	CHECK(state.effective_scale(1120) == doctest::Approx(0.0));
	CHECK(state.effective_scale(1200) == doctest::Approx(0.25)); // Hitstop over, slow motion resumes.
	CHECK(state.effective_scale(1500) == doctest::Approx(1.0)); // Both over: the original base.
	CHECK_FALSE(state.active(1500));
}

TEST_CASE("[Scene][UZUJuice] Time state: the base scale is what the game had before") {
	UZUJuiceTimeState state;
	state.start_slowmo(0, 0.5, 0.2, 0.8); // The game itself was running at 0.8.
	CHECK(state.effective_scale(300) == doctest::Approx(0.8));
	state.cancel();
	CHECK_FALSE(state.active(0));
	CHECK(state.effective_scale(0) == doctest::Approx(0.8));
}

TEST_CASE("[Scene][UZUJuice] Static API drives the shared state") {
	UZUJuice::cancel_time_effects();
	CHECK_FALSE(UZUJuice::is_time_effect_active());
	UZUJuice::hitstop(0.05, 0.0);
	CHECK(UZUJuice::is_time_effect_active());
	CHECK(UZUJuice::applied_time_scale() == doctest::Approx(0.0));
	UZUJuice::cancel_time_effects();
	CHECK_FALSE(UZUJuice::is_time_effect_active());
}

TEST_CASE("[Scene][UZUJuice] Shake state: trauma clamps, decays and squares") {
	UZUJuiceShakeState state;
	CHECK(state.strength() == doctest::Approx(0.0));
	state.add(0.3);
	CHECK(state.trauma == doctest::Approx(0.3));
	state.add(0.9);
	CHECK(state.trauma == doctest::Approx(1.0)); // Clamped.
	CHECK(state.strength() == doctest::Approx(1.0));
	state.update(0.3125); // 1.6 per second: half a second of trauma in 0.3125 s.
	CHECK(state.trauma == doctest::Approx(0.5));
	CHECK(state.strength() == doctest::Approx(0.25)); // Squared: half trauma, quarter shake.
	state.update(10.0);
	CHECK(state.trauma == doctest::Approx(0.0)); // Never below zero.
}

TEST_CASE("[Scene][UZUJuice] Shake state: the offset is bounded and deterministic") {
	UZUJuiceShakeState state;
	CHECK(state.offset_at(3.7, 0.5) == Vector2());
	state.add(1.0);
	Vector2 a = state.offset_at(3.7, 0.5);
	Vector2 b = state.offset_at(3.7, 0.5);
	CHECK(a == b); // Same time, same offset.
	CHECK(Math::abs(a.x) <= 0.5);
	CHECK(Math::abs(a.y) <= 0.5);
	state.clear();
	CHECK(state.offset_at(3.7, 0.5) == Vector2());
}

TEST_CASE("[Scene][UZUJuice] Shake static API drives the shared state") {
	UZUJuice::clear_shake();
	CHECK(UZUJuice::get_trauma() == doctest::Approx(0.0));
	UZUJuice::shake(0.4);
	CHECK(UZUJuice::get_trauma() == doctest::Approx(0.4));
	UZUJuice::shake(0.4);
	CHECK(UZUJuice::get_trauma() == doctest::Approx(0.8));
	UZUJuice::clear_shake();
	CHECK(UZUJuice::get_trauma() == doctest::Approx(0.0));
}

TEST_CASE("[Scene][UZUJuice] Spring state: displaced spring overshoots and settles") {
	UZUJuiceSpringState spring;
	spring.displace(0.3);
	CHECK(spring.x == doctest::Approx(0.3));
	bool crossed_zero = false;
	for (int i = 0; i < 600; i++) {
		spring.update(1.0 / 60.0);
		CHECK(Math::abs(spring.x) <= 0.31);
		if (spring.x < 0.0) {
			crossed_zero = true;
		}
	}
	CHECK(crossed_zero); // Underdamped: it wobbles back past zero.
	CHECK(spring.settled());
}

TEST_CASE("[Scene][UZUJuice] Flash tints a sprite and restores it exactly") {
	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_modulate(Color(0.5, 0.5, 0.5, 1.0));
	UZUJuice::flash(sprite, Color(1, 0, 0), 5.0);
	UZUJuice::apply_node_effects(0.0);
	Color during = sprite->get_modulate();
	CHECK(during.r == doctest::Approx(1.0).epsilon(0.01));
	CHECK(during.g == doctest::Approx(0.0).epsilon(0.01));
	UZUJuice::flash(sprite, Color(1, 0, 0), 0.0); // Zero duration: ends at once.
	UZUJuice::apply_node_effects(0.0);
	CHECK(sprite->get_modulate() == Color(0.5, 0.5, 0.5, 1.0));
	memdelete(sprite);
	UZUJuice::apply_node_effects(0.0); // Purges the stale entry.
}

TEST_CASE("[Scene][UZUJuice] Squash deforms a Node3D and the spring restores it") {
	Node3D *node = memnew(Node3D);
	UZUJuice::squash(node, 0.3);
	UZUJuice::apply_node_effects(0.0);
	Vector3 during = node->get_scale();
	CHECK(during.x == doctest::Approx(1.3));
	CHECK(during.y == doctest::Approx(0.76));
	for (int i = 0; i < 600; i++) {
		UZUJuice::apply_node_effects(1.0 / 60.0);
	}
	CHECK(node->get_scale() == Vector3(1, 1, 1));
	memdelete(node);
	UZUJuice::apply_node_effects(0.0);
}

TEST_CASE("[Scene][UZUJuice] Kick punches the zoom and springs back") {
	UZUJuice::kick(0.5);
	CHECK(UZUJuice::get_kick() == doctest::Approx(0.5));
	CHECK(UZUJuice::kick_state().fov_delta() == doctest::Approx(-4.0));
	CHECK(UZUJuice::kick_state().zoom_factor() == doctest::Approx(1.125));
	for (int i = 0; i < 600; i++) {
		UZUJuice::kick_state().update(1.0 / 60.0);
	}
	CHECK(UZUJuice::kick_state().settled());
	CHECK(Math::abs(UZUJuice::get_kick()) < 0.01);
}

TEST_CASE("[Scene][UZUJuice] Burst3D is configured one-shot and frees itself") {
	Node3D *parent = memnew(Node3D);
	CPUParticles3D *burst = UZUJuice::burst3d(parent, Vector3(1, 2, 3), Color(1, 0.5, 0), 20, 0.25);
	REQUIRE(burst != nullptr);
	CHECK(burst->get_amount() == 20);
	CHECK(burst->get_one_shot());
	CHECK(burst->is_emitting());
	CHECK(burst->get_lifetime() == doctest::Approx(0.55 * 0.25));
	ObjectID id = burst->get_instance_id();
	OS::get_singleton()->delay_usec(500000); // Past the burst's free-at time.
	UZUJuice::apply_node_effects(0.0);
	CHECK(ObjectDB::get_instance(id) == nullptr);
	memdelete(parent);
}

TEST_CASE("[Scene][UZUJuice] Burst2D carries the dot texture and the color") {
	Node2D *parent = memnew(Node2D);
	CPUParticles2D *burst = UZUJuice::burst2d(parent, Vector2(4, 5), Color(0, 1, 0), 9, 1.0);
	REQUIRE(burst != nullptr);
	CHECK(burst->get_amount() == 9);
	CHECK(burst->get_one_shot());
	CHECK(burst->get_texture().is_valid());
	CHECK(burst->get_color() == Color(0, 1, 0));
	memdelete(parent);
	UZUJuice::apply_node_effects(0.0); // Purges the stale entry.
}

} // namespace TestUZUJuice


TEST_CASE("[Scene][UZUJuice] impact debug steps (temporary)") {
	fprintf(stderr, "DBG step 0\n");
	Node3D *parent = memnew(Node3D);
	Node3D *node = memnew(Node3D);
	parent->add_child(node);
	UZUJuice::squash(node, 0.3);
	fprintf(stderr, "DBG step 1 squash ok\n");
	UZUJuice::shake(0.12);
	fprintf(stderr, "DBG step 2 shake ok\n");
	Ref<AudioStreamWAV> stream = UZUSfx::generate_preset("land");
	fprintf(stderr, "DBG step 3 generate ok valid=%d\n", stream.is_valid() ? 1 : 0);
	AudioStreamPlayer *player = UZUSfx::play(parent, stream);
	fprintf(stderr, "DBG step 4 play ok player=%p\n", (void *)player);
	memdelete(parent);
	fprintf(stderr, "DBG step 5 delete ok\n");
	UZUJuice::clear_shake();
}

TEST_CASE("[Scene][UZUJuice] impact land: squash, shake and sound only") {
	Node3D *parent = memnew(Node3D);
	Node3D *node = memnew(Node3D);
	parent->add_child(node);
	CHECK(UZUJuice::impact(node, "land"));
	CHECK(UZUJuice::get_trauma() > 0.0);
	UZUJuice::clear_shake();
	memdelete(parent);
}

TEST_CASE("[Scene][UZUJuice] impact pickup: burst and sound, no hitstop") {
	Node3D *parent = memnew(Node3D);
	Node3D *node = memnew(Node3D);
	parent->add_child(node);
	CHECK(UZUJuice::impact(node, "pickup"));
	CHECK_FALSE(UZUJuice::is_time_effect_active());
	memdelete(parent);
}

TEST_CASE("[Scene][UZUJuice] impact hit and unknown kinds") {
	PackedStringArray names = UZUJuice::get_impact_preset_names();
	CHECK(names.size() == 5);
	CHECK(names.has("hit"));
	CHECK(names.has("pickup"));

	Node3D *parent = memnew(Node3D);
	Node3D *node = memnew(Node3D);
	parent->add_child(node);
	CHECK(UZUJuice::impact(node, "hit"));
	CHECK(UZUJuice::is_time_effect_active()); // hitstop engaged.
	UZUJuice::cancel_time_effects();
	CHECK_FALSE(UZUJuice::impact(node, "explode_everything"));
	memdelete(parent);
	UZUJuice::cancel_time_effects();
}
