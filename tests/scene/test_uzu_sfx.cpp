/**************************************************************************/
/*  test_uzu_sfx.cpp                                                      */
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

TEST_FORCE_LINK(test_uzu_sfx)

#include "core/object/callable_mp.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/node.h"
#include "scene/resources/uzu_sfx.h"
#include "scene/scene_string_names.h"

namespace TestUZUSfx {

TEST_CASE("[Audio][UZUSfx] A tone has the length, pitch and envelope asked for") {
	Dictionary params;
	params["wave"] = "sine";
	params["frequency"] = 440.0;
	params["frequency_end"] = 440.0;
	params["attack"] = 0.01;
	params["sustain"] = 0.2;
	params["decay"] = 0.1;
	params["volume"] = 0.8;
	const Ref<AudioStreamWAV> s = UZUSfx::generate(params);
	REQUIRE(s.is_valid());
	CHECK(s->get_format() == AudioStreamWAV::FORMAT_16_BITS);
	CHECK_FALSE(s->is_stereo());
	CHECK(s->get_mix_rate() == 44100);
	const Dictionary d = UZUSfx::describe(s);
	CHECK(double(d["length"]) == doctest::Approx(0.31).epsilon(0.01));
	CHECK(double(d["peak"]) == doctest::Approx(0.8).epsilon(0.02));
	CHECK(double(d["rms"]) > 0.3); // Not silent.
	// A sine at 440 Hz crosses zero 880 times a second.
	CHECK(double(d["pitch_start"]) == doctest::Approx(880.0).epsilon(0.03));
	CHECK(bool(d["ends_silent"]));
}

TEST_CASE("[Audio][UZUSfx] Slides, pitch jumps and noise") {
	Dictionary rising;
	rising["wave"] = "triangle";
	rising["frequency"] = 200.0;
	rising["frequency_end"] = 800.0;
	const Dictionary up = UZUSfx::describe(UZUSfx::generate(rising));
	CHECK(double(up["pitch_end"]) > double(up["pitch_start"]) * 2.0);

	Dictionary jump;
	jump["wave"] = "square";
	jump["frequency"] = 500.0;
	jump["frequency_end"] = 500.0;
	jump["change_ratio"] = 2.0;
	jump["change_time"] = 0.05;
	jump["sustain"] = 0.2;
	jump["decay"] = 0.05;
	const Dictionary j = UZUSfx::describe(UZUSfx::generate(jump));
	CHECK(double(j["pitch_end"]) == doctest::Approx(double(j["pitch_start"]) * 2.0).epsilon(0.08));

	// Noise at a high "frequency" crosses zero far more often than a tone at that pitch would not.
	Dictionary noise;
	noise["wave"] = "noise";
	noise["frequency"] = 4000.0;
	noise["frequency_end"] = 4000.0;
	const Dictionary n = UZUSfx::describe(UZUSfx::generate(noise));
	CHECK(double(n["pitch_start"]) > 2000.0);
	CHECK(double(n["rms"]) > 0.1);
	// The low-pass takes the edge off.
	noise["lowpass"] = 0.2;
	CHECK(double(UZUSfx::describe(UZUSfx::generate(noise))["pitch_start"]) < double(n["pitch_start"]));
}

TEST_CASE("[Audio][UZUSfx] Presets") {
	const PackedStringArray names = UZUSfx::get_preset_names();
	for (const char *required : { "hit", "jump", "pickup", "coin", "explosion", "laser", "powerup", "blip", "land" }) {
		CHECK_MESSAGE(names.has(required), required);
	}
	for (const String &name : names) {
		const Ref<AudioStreamWAV> s = UZUSfx::generate_preset(name);
		REQUIRE_MESSAGE(s.is_valid(), name);
		const Dictionary d = UZUSfx::describe(s);
		CHECK_MESSAGE(double(d["length"]) > 0.02, name);
		CHECK_MESSAGE(double(d["length"]) < 1.5, name); // Game sounds are short.
		CHECK_MESSAGE(double(d["peak"]) > 0.2, name);
		CHECK_MESSAGE(double(d["peak"]) <= 1.0, name);
		CHECK_MESSAGE(bool(d["ends_silent"]), name);
	}
	// A jump rises; a laser and an explosion fall.
	const Dictionary jump = UZUSfx::describe(UZUSfx::generate_preset("jump"));
	CHECK(double(jump["pitch_end"]) > double(jump["pitch_start"]));
	const Dictionary laser = UZUSfx::describe(UZUSfx::generate_preset("laser"));
	CHECK(double(laser["pitch_end"]) < double(laser["pitch_start"]));

	// Seeds: the same seed gives the same sound, another seed a variation, seed 0 the preset.
	CHECK(UZUSfx::generate_preset("coin", 5)->get_data() == UZUSfx::generate_preset("coin", 5)->get_data());
	CHECK(UZUSfx::generate_preset("coin", 5)->get_data() != UZUSfx::generate_preset("coin", 6)->get_data());
	CHECK(double(UZUSfx::get_preset("coin")["frequency"]) == doctest::Approx(990.0));
	const double varied = UZUSfx::get_preset("coin", 6)["frequency"];
	CHECK(varied != doctest::Approx(990.0));
	CHECK(varied > 990.0 * 0.86);
	CHECK(varied < 990.0 * 1.16);
}

TEST_CASE("[Audio][UZUSfx] Wrong parameters") {
	ERR_PRINT_OFF;
	Dictionary unknown;
	unknown["frequncy"] = 440.0;
	CHECK(UZUSfx::generate(unknown).is_null());
	Dictionary wave;
	wave["wave"] = "kazoo";
	CHECK(UZUSfx::generate(wave).is_null());
	Dictionary silent;
	silent["sustain"] = 0.0;
	silent["decay"] = 0.0;
	CHECK(UZUSfx::generate(silent).is_null());
	CHECK(UZUSfx::get_preset("nope").is_empty());
	CHECK(UZUSfx::generate_preset("nope").is_null());
	ERR_PRINT_ON;
}

TEST_CASE("[Audio][UZUSfx] Play with a little variation") {
	// The parent is outside the tree: the players are set up but do not start here.
	Node *parent = memnew(Node);
	const Ref<AudioStreamWAV> s = UZUSfx::generate_preset("blip");
	for (int i = 0; i < 20; i++) {
		AudioStreamPlayer *p = UZUSfx::play(parent, s, 0.1, 2.0, -6.0);
		REQUIRE(p != nullptr);
		CHECK(p->get_parent() == parent);
		CHECK(p->get_stream() == s);
		CHECK(p->get_pitch_scale() >= 0.9f);
		CHECK(p->get_pitch_scale() <= 1.1f);
		CHECK(p->get_volume_db() >= -8.0f);
		CHECK(p->get_volume_db() <= -4.0f);
		CHECK(p->is_autoplay_enabled());
		CHECK(p->is_connected(SceneStringName(finished), callable_mp(static_cast<Node *>(p), &Node::queue_free)));
	}
	CHECK(parent->get_child_count() == 20);
	ERR_PRINT_OFF;
	CHECK(UZUSfx::play(nullptr, s) == nullptr);
	CHECK(UZUSfx::play(parent, Ref<AudioStream>()) == nullptr);
	ERR_PRINT_ON;
	memdelete(parent);
}

} // namespace TestUZUSfx
