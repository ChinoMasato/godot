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

#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "scene/resources/audio/audio_stream_wav.h"

class AudioStreamPlayer;
class Node;

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
};
