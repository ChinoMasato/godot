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

TEST_CASE("[Audio][UZUSfx] Sprite manifest is a pure, exact layout") {
	// No stream is generated here: the manifest comes from the preset
	// parameters alone and must match the generated lengths exactly.
	const PackedStringArray names = { "hit", "jump", "pickup", "coin" };
	const Dictionary manifest = UZUSfx::sprite_manifest(names, 50, 0);
	REQUIRE(manifest.size() == 4);
	const int rate = 44100;
	const int gap = 50 * rate / 1000;
	int cursor = 0;
	for (const String &name : names) {
		const Dictionary seg = UZUSfx::get_sprite_segment(manifest, name);
		REQUIRE(!seg.is_empty());
		const Ref<AudioStreamWAV> single = UZUSfx::generate_preset(name);
		const int length = single->get_data().size() / 2;
		CHECK(int(seg["start_sample"]) == cursor);
		CHECK(int(seg["length_samples"]) == length);
		CHECK(int(seg["end_sample"]) == cursor + length);
		CHECK(double(seg["start_sec"]) == doctest::Approx(double(cursor) / rate).epsilon(0.0001));
		CHECK(double(seg["duration_sec"]) == doctest::Approx(double(length) / rate).epsilon(0.0001));
		CHECK(double(seg["end_sec"]) == doctest::Approx(double(cursor + length) / rate).epsilon(0.0001));
		cursor += length + gap;
	}
	// Gap 0 packs the sounds back to back; a bigger gap shifts later segments.
	const Dictionary tight = UZUSfx::sprite_manifest(names, 0, 0);
	CHECK(int(UZUSfx::get_sprite_segment(tight, "jump")["start_sample"]) == int(UZUSfx::get_sprite_segment(manifest, "hit")["length_samples"]));
	const Dictionary wide = UZUSfx::sprite_manifest(names, 200, 0);
	CHECK(int(UZUSfx::get_sprite_segment(wide, "jump")["start_sample"]) > int(UZUSfx::get_sprite_segment(manifest, "jump")["start_sample"]));
	// The same seed varies the layout the same way every time.
	CHECK(UZUSfx::sprite_manifest(names, 50, 7) == UZUSfx::sprite_manifest(names, 50, 7));
	CHECK(UZUSfx::sprite_manifest(names, 50, 7) != manifest);
	// Unknown names and empty sprites are errors, not empty layouts.
	ERR_PRINT_OFF;
	CHECK(UZUSfx::sprite_manifest(PackedStringArray({ "nope" })).is_empty());
	CHECK(UZUSfx::sprite_manifest(PackedStringArray()).is_empty());
	CHECK(UZUSfx::get_sprite_segment(manifest, "nope").is_empty());
	ERR_PRINT_ON;
}

TEST_CASE("[Audio][UZUSfx] Sprite packs sounds into one stream, deterministically") {
	const PackedStringArray names = { "hit", "jump", "pickup", "coin" };
	const Dictionary built = UZUSfx::build_sprite(names, 50, 0);
	REQUIRE(!built.is_empty());
	const Ref<AudioStreamWAV> sprite = built["stream"];
	REQUIRE(sprite.is_valid());
	CHECK(sprite->get_format() == AudioStreamWAV::FORMAT_16_BITS);
	CHECK_FALSE(sprite->is_stereo());
	CHECK(sprite->get_mix_rate() == 44100);
	const Dictionary manifest = built["manifest"];
	CHECK(manifest == UZUSfx::sprite_manifest(names, 50, 0));
	const int total = sprite->get_data().size() / 2;
	CHECK(int(built["total_samples"]) == total);
	// The total is the last segment's end: sounds plus the gaps between them.
	const Dictionary last = UZUSfx::get_sprite_segment(manifest, "coin");
	CHECK(total == int(last["end_sample"]));
	// Each segment's samples are exactly the preset's own samples, and the
	// gaps between segments are silence.
	const Vector<uint8_t> data = sprite->get_data();
	auto sample_at = [&](int i) {
		return int16_t(uint16_t(data[i * 2]) | (uint16_t(data[i * 2 + 1]) << 8));
	};
	for (const String &name : names) {
		const Dictionary seg = UZUSfx::get_sprite_segment(manifest, name);
		const Vector<uint8_t> single = UZUSfx::generate_preset(name)->get_data();
		const int start = int(seg["start_sample"]);
		bool same = true;
		for (int i = 0; i < single.size(); i++) {
			same = same && data[start * 2 + i] == single[i];
		}
		CHECK_MESSAGE(same, name);
	}
	const Dictionary hit = UZUSfx::get_sprite_segment(manifest, "hit");
	const Dictionary jump = UZUSfx::get_sprite_segment(manifest, "jump");
	bool gap_silent = true;
	for (int i = int(hit["end_sample"]); i < int(jump["start_sample"]); i++) {
		gap_silent = gap_silent && sample_at(i) == 0;
	}
	CHECK(gap_silent);
	CHECK(int(jump["start_sample"]) - int(hit["end_sample"]) == int(built["gap_samples"]));
	// Deterministic: the same input builds the same bytes and manifest.
	// (Compared as bools: a failed == on PackedByteArray would print both
	// whole streams into the test log.)
	const Dictionary again = UZUSfx::build_sprite(names, 50, 0);
	const Ref<AudioStreamWAV> sprite2 = again["stream"];
	const Vector<uint8_t> data2 = sprite2->get_data();
	bool identical = data2.size() == data.size();
	for (int i = 0; identical && i < data.size(); i++) {
		identical = data2[i] == data[i];
	}
	CHECK(identical);
	CHECK(Dictionary(again["manifest"]) == manifest);
	// Another seed gives a different sprite.
	const Ref<AudioStreamWAV> varied = UZUSfx::generate_sprite(names, 50, 3);
	const Vector<uint8_t> varied_data = varied->get_data();
	bool varied_differs = varied_data.size() != data.size();
	for (int i = 0; !varied_differs && i < data.size(); i++) {
		varied_differs = varied_data[i] != data[i];
	}
	CHECK(varied_differs);
	ERR_PRINT_OFF;
	CHECK(UZUSfx::generate_sprite(PackedStringArray({ "nope" })).is_null());
	CHECK(UZUSfx::build_sprite(PackedStringArray()).is_empty());
	ERR_PRINT_ON;
}

TEST_CASE("[Audio][UZUSfx] Sprite segment playback setup and stop condition") {
	const PackedStringArray names = { "hit", "jump", "pickup", "coin" };
	const Dictionary built = UZUSfx::build_sprite(names, 50, 0);
	const Ref<AudioStreamWAV> sprite = built["stream"];
	const Dictionary manifest = built["manifest"];
	// The pure stop condition: inside the segment playback continues, at
	// (or just before) its end it must stop, and other segments do not leak.
	const Dictionary jump = UZUSfx::get_sprite_segment(manifest, "jump");
	CHECK_FALSE(UZUSfx::sprite_should_stop(double(jump["start_sec"]) + 0.01, jump));
	CHECK(UZUSfx::sprite_should_stop(double(jump["end_sec"]), jump));
	CHECK(UZUSfx::sprite_should_stop(double(jump["end_sec"]) + 0.5, jump));
	CHECK(UZUSfx::sprite_should_stop(0.0, Dictionary())); // No segment: stop.
	const UZUSfxSpriteSegment seg = { int(jump["start_sample"]), int(jump["length_samples"]), 44100 };
	CHECK(seg.contains(double(jump["start_sec"]) + 0.01));
	CHECK_FALSE(seg.contains(double(jump["end_sec"]) + 0.01));
	CHECK(seg.should_stop(double(jump["end_sec"])));
	// play_sprite sets a player up on the segment: right stream, segment in
	// its metadata, registered for the runtime to stop at the segment end.
	Node *parent = memnew(Node);
	AudioStreamPlayer *player = UZUSfx::play_sprite(parent, sprite, manifest, "jump", -3.0);
	REQUIRE(player != nullptr);
	CHECK(player->get_parent() == parent);
	CHECK(player->get_stream() == sprite);
	CHECK(player->get_volume_db() == doctest::Approx(-3.0));
	CHECK(String(player->get_meta("uzu_sprite_name")) == "jump");
	CHECK(Dictionary(player->get_meta("uzu_sprite_segment")) == jump);
	UZUSfx::apply_sprite_playbacks(); // Must be safe with a fresh player.
	ERR_PRINT_OFF;
	CHECK(UZUSfx::play_sprite(parent, sprite, manifest, "nope") == nullptr);
	CHECK(UZUSfx::play_sprite(nullptr, sprite, manifest, "jump") == nullptr);
	CHECK(UZUSfx::play_sprite(parent, Ref<AudioStream>(), manifest, "jump") == nullptr);
	ERR_PRINT_ON;
	memdelete(parent); // Frees the player too; the registry prunes its entry.
	UZUSfx::apply_sprite_playbacks();
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
