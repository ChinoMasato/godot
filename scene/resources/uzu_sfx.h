/**************************************************************************/
/*  uzu_sfx.h                                                             */
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

#include "core/math/math_funcs.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "scene/resources/audio/audio_stream_wav.h"

class AudioStreamPlayer;
class Node;

// One segment of an audio sprite (BACKLOG B-1108): where one sound sits inside
// the packed stream. Pure values, so the segment math (offsets, the stop
// condition for segment playback) is unit-testable without an AudioServer or
// a scene tree, in the same style as UZUJuice's state structs.
struct UZUSfxSpriteSegment {
	int start_sample = 0;
	int length_samples = 0;
	int sample_rate = 44100;

	int end_sample() const { return start_sample + length_samples; }
	double start_sec() const { return sample_rate > 0 ? double(start_sample) / double(sample_rate) : 0.0; }
	double duration_sec() const { return sample_rate > 0 ? double(length_samples) / double(sample_rate) : 0.0; }
	double end_sec() const { return start_sec() + duration_sec(); }
	// Playback of this segment must stop once the player's position reaches
	// the segment end (a small tolerance keeps the last samples audible).
	bool should_stop(double p_playback_pos_sec) const { return p_playback_pos_sec >= end_sec() - 0.002; }
	bool contains(double p_playback_pos_sec) const { return p_playback_pos_sec >= start_sec() && p_playback_pos_sec < end_sec(); }
	Dictionary to_dictionary() const {
		Dictionary d;
		d["start_sample"] = start_sample;
		d["length_samples"] = length_samples;
		d["end_sample"] = end_sample();
		d["start_sec"] = start_sec();
		d["duration_sec"] = duration_sec();
		d["end_sec"] = end_sec();
		return d;
	}
	static UZUSfxSpriteSegment from_dictionary(const Dictionary &p_dict) {
		UZUSfxSpriteSegment s;
		s.start_sample = int(p_dict.get("start_sample", 0));
		s.length_samples = int(p_dict.get("length_samples", 0));
		// start_sec/duration_sec carry the rate implicitly when present.
		if (p_dict.has("start_sec") && double(p_dict["start_sec"]) > 0.0 && s.start_sample > 0) {
			s.sample_rate = int(Math::round(double(s.start_sample) / double(p_dict["start_sec"])));
		}
		return s;
	}
};

// U.ZU sound effects (BACKLOG B-1105, UZUJuice): game sounds synthesized from a few
// parameters, sfxr style, so a game needs no sound files for hits, jumps and pickups.
// A preset table (hit, jump, pickup, coin, explosion, laser, powerup, blip, land) gives
// a starting point, varied by a seed; play() adds a little random pitch and volume so
// repeated sounds do not feel mechanical. UZUJuice (B-1106) builds on this.
class UZUSfx : public Object {
	GDCLASS(UZUSfx, Object);

protected:
	static void _bind_methods();

public:
	// Parameters and their defaults (see get_defaults()):
	//   wave ("square", "saw", "sine", "triangle", "noise"), frequency and
	//   frequency_end (Hz; the pitch slides between them over the sound),
	//   change_ratio and change_time (a pitch jump, as in coin sounds), vibrato_depth
	//   (semitones) and vibrato_speed (Hz), duty and duty_sweep (square wave),
	//   attack, sustain, decay (seconds), punch (0..1, a boost at the start of the
	//   sustain), lowpass (0..1, 1 = off), volume (0..1), seed (noise), sample_rate.
	static Dictionary get_defaults();
	static PackedStringArray get_preset_names();
	// The parameters of a preset. Seed 0 is the preset itself; other seeds vary its
	// pitch and length a little (the same seed gives the same sound).
	static Dictionary get_preset(const String &p_name, int p_seed = 0);
	// A 16-bit mono AudioStreamWAV, or null (with an error) for an unknown parameter or value.
	static Ref<AudioStreamWAV> generate(const Dictionary &p_params);
	static Ref<AudioStreamWAV> generate_preset(const String &p_name, int p_seed = 0);
	// What a sound is like, for an agent that cannot listen: {"length", "peak", "rms",
	// "pitch_start", "pitch_end" (zero crossings per second over the first and last
	// fifth, about twice the pitch for tones), "ends_silent" (the last millisecond is
	// under 5 % of the peak: no click at the end)}.
	static Dictionary describe(const Ref<AudioStreamWAV> &p_stream);
	// Plays p_stream once under p_parent with a random pitch (1 +- p_pitch_jitter) and
	// volume (p_volume_db +- p_volume_jitter_db); the player frees itself when done.
	static AudioStreamPlayer *play(Node *p_parent, const Ref<AudioStream> &p_stream, float p_pitch_jitter = 0.05, float p_volume_jitter_db = 1.0, float p_volume_db = 0.0);

	// --- Audio sprites (BACKLOG B-1108) -------------------------------------
	// Several preset sounds packed into one AudioStreamWAV, separated by a
	// fixed silence gap, so a game ships and loads one audio file instead of
	// many. The manifest maps each name to its segment; generation and the
	// manifest come from the same segment computation, so they cannot drift.
	// The manifest alone is a pure computation from the preset parameters
	// (no AudioServer needed); the packed stream needs one, like generate().
	// A repeated name keeps its first segment in the manifest.
	static Dictionary sprite_manifest(const PackedStringArray &p_names, int p_gap_ms = 50, int p_seed = 0);
	// The packed stream: the presets in order, gap_ms of silence between
	// neighbours (not before the first or after the last). Deterministic:
	// the same names, gap and seed give the same samples and manifest.
	static Ref<AudioStreamWAV> generate_sprite(const PackedStringArray &p_names, int p_gap_ms = 50, int p_seed = 0);
	// {"stream", "manifest", "sample_rate", "gap_ms", "gap_samples",
	// "total_samples", "total_sec"} in one call, for agents and the CLI.
	static Dictionary build_sprite(const PackedStringArray &p_names, int p_gap_ms = 50, int p_seed = 0);
	// The segment of one name in a manifest, or an empty Dictionary.
	static Dictionary get_sprite_segment(const Dictionary &p_manifest, const String &p_name);
	// True once a player at p_playback_pos_sec has reached the segment's end
	// and must stop (the pure stop condition used by segment playback).
	static bool sprite_should_stop(double p_playback_pos_sec, const Dictionary &p_segment);
	// Plays only the named segment of a sprite under p_parent: an
	// AudioStreamPlayer that starts at the segment and is stopped (and
	// freed) at its end by the sprite runtime node. The segment is kept in
	// the player's "uzu_sprite_segment" metadata. Playback starts as soon
	// as the player is inside the scene tree (at once, when the parent
	// already is).
	static AudioStreamPlayer *play_sprite(Node *p_parent, const Ref<AudioStream> &p_sprite, const Dictionary &p_manifest, const String &p_name, float p_volume_db = 0.0);
	// Stops any registered segment players that reached their end; called
	// every frame by the sprite runtime node. Exposed for tests.
	static void apply_sprite_playbacks();
	// Creates the sprite runtime node in the scene tree if there is none.
	static void ensure_sprite_runtime();
};
