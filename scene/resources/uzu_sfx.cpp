/**************************************************************************/
/*  uzu_sfx.cpp                                                           */
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

#include "uzu_sfx.h"
#include <cstdio>

#include "core/math/random_pcg.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/node.h"
#include "scene/scene_string_names.h"

namespace {

const char *WAVES[] = { "square", "saw", "sine", "triangle", "noise" };

Dictionary defaults() {
	Dictionary d;
	d["wave"] = "square";
	d["frequency"] = 440.0;
	d["frequency_end"] = 440.0;
	d["change_ratio"] = 1.0;
	d["change_time"] = 0.0;
	d["vibrato_depth"] = 0.0;
	d["vibrato_speed"] = 0.0;
	d["duty"] = 0.5;
	d["duty_sweep"] = 0.0;
	d["attack"] = 0.0;
	d["sustain"] = 0.1;
	d["punch"] = 0.0;
	d["decay"] = 0.2;
	d["lowpass"] = 1.0;
	d["volume"] = 0.5;
	d["seed"] = 1;
	d["sample_rate"] = 44100;
	return d;
}

struct Preset {
	const char *name;
	const char *wave;
	float frequency, frequency_end, change_ratio, change_time, vibrato_depth, vibrato_speed, duty, duty_sweep, attack, sustain, punch, decay, lowpass;
};

// Hand-tuned starting points, in the spirit of sfxr's generators.
const Preset PRESETS[] = {
	// name        wave        freq    end    chg   chg_t  vib  vib_s duty  sweep  att   sus    punch dec   lowpass
	{ "hit", "noise", 1600, 300, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.03, 0.4, 0.14, 0.8 },
	{ "jump", "square", 280, 720, 1.0, 0.0, 0.0, 0.0, 0.35, 0.2, 0.0, 0.09, 0.0, 0.14, 1.0 },
	{ "pickup", "square", 700, 760, 1.5, 0.06, 0.0, 0.0, 0.5, 0.0, 0.0, 0.05, 0.35, 0.18, 1.0 },
	{ "coin", "square", 990, 990, 1.335, 0.07, 0.0, 0.0, 0.5, 0.0, 0.0, 0.06, 0.45, 0.3, 1.0 },
	{ "explosion", "noise", 900, 120, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.12, 0.6, 0.7, 0.5 },
	{ "laser", "saw", 1500, 220, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.08, 0.2, 0.16, 0.9 },
	{ "powerup", "square", 300, 900, 1.0, 0.0, 0.6, 12.0, 0.4, -0.2, 0.0, 0.25, 0.0, 0.3, 1.0 },
	{ "blip", "square", 820, 820, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.04, 0.0, 0.05, 1.0 },
	{ "land", "noise", 420, 90, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.02, 0.3, 0.11, 0.35 },
};

Ref<AudioStreamWAV> fail(const String &p_error) {
	ERR_FAIL_V_MSG(Ref<AudioStreamWAV>(), "UZUSfx: " + p_error);
}

} // namespace

Dictionary UZUSfx::get_defaults() {
	return defaults();
}

PackedStringArray UZUSfx::get_preset_names() {
	PackedStringArray names;
	for (const Preset &p : PRESETS) {
		names.push_back(p.name);
	}
	return names;
}

Dictionary UZUSfx::get_preset(const String &p_name, int p_seed) {
	for (const Preset &p : PRESETS) {
		if (p_name != p.name) {
			continue;
		}
		// Seed 0 is the preset as written; others shift pitch (+- 0.2 octave) and
		// length (+- 15 %) a little, the same way for the same seed.
		float pitch = 1.0;
		float length = 1.0;
		if (p_seed != 0) {
			RandomPCG rng(uint64_t(p_seed) * 7919u + 17u);
			pitch = Math::pow(2.0f, (rng.randf() * 2.0f - 1.0f) * 0.2f);
			length = 0.85f + rng.randf() * 0.3f;
		}
		Dictionary d = defaults();
		d["wave"] = p.wave;
		d["frequency"] = p.frequency * pitch;
		d["frequency_end"] = p.frequency_end * pitch;
		d["change_ratio"] = p.change_ratio;
		d["change_time"] = p.change_time * length;
		d["vibrato_depth"] = p.vibrato_depth;
		d["vibrato_speed"] = p.vibrato_speed;
		d["duty"] = p.duty;
		d["duty_sweep"] = p.duty_sweep;
		d["attack"] = p.attack * length;
		d["sustain"] = p.sustain * length;
		d["punch"] = p.punch;
		d["decay"] = p.decay * length;
		d["lowpass"] = p.lowpass;
		d["seed"] = p_seed == 0 ? 1 : p_seed;
		return d;
	}
	ERR_FAIL_V_MSG(Dictionary(), vformat("UZUSfx: unknown preset \"%s\" (see get_preset_names).", p_name));
}

Ref<AudioStreamWAV> UZUSfx::generate(const Dictionary &p_params) {
	Dictionary params = defaults();
	for (const Variant &key : p_params.keys()) {
		if (!params.has(key)) {
			return fail(vformat("unknown parameter \"%s\" (see get_defaults).", String(key)));
		}
		params[key] = p_params[key];
	}
	int wave = -1;
	for (int i = 0; i < 5; i++) {
		if (String(params["wave"]) == WAVES[i]) {
			wave = i;
		}
	}
	if (wave < 0) {
		return fail(vformat("unknown wave \"%s\" (square, saw, sine, triangle or noise).", String(params["wave"])));
	}
	const int rate = CLAMP(int(params["sample_rate"]), 8000, 96000);
	const double attack = MAX(0.0, double(params["attack"]));
	const double sustain = MAX(0.0, double(params["sustain"]));
	const double decay = MAX(0.0, double(params["decay"]));
	const double total = attack + sustain + decay;
	if (total <= 0.0 || total > 10.0) {
		return fail("attack + sustain + decay must be between 0 and 10 seconds.");
	}
	const double f0 = CLAMP(double(params["frequency"]), 10.0, 20000.0);
	const double f1 = CLAMP(double(params["frequency_end"]), 10.0, 20000.0);
	const double change_ratio = MAX(0.01, double(params["change_ratio"]));
	const double change_time = double(params["change_time"]);
	const double vibrato_depth = double(params["vibrato_depth"]);
	const double vibrato_speed = double(params["vibrato_speed"]);
	const double duty = double(params["duty"]);
	const double duty_sweep = double(params["duty_sweep"]);
	const double punch = CLAMP(double(params["punch"]), 0.0, 1.0);
	const double lowpass = CLAMP(double(params["lowpass"]), 0.0, 1.0);
	const double volume = CLAMP(double(params["volume"]), 0.0, 1.0);
	const uint64_t seed = uint64_t(int64_t(params["seed"]));
	RandomPCG rng(seed);

	const int count = int(total * rate);
	Vector<uint8_t> data;
	data.resize(count * 2);
	uint8_t *w = data.ptrw();
	double phase = 0.0;
	double noise_value = rng.randf() * 2.0 - 1.0;
	double filtered = 0.0;
	// One-pole low-pass: 1 is off, smaller is darker (cutoff from about 20 kHz down).
	const double cutoff = lowpass >= 1.0 ? 1.0 : Math::pow(lowpass, 2.0) * 0.98 + 0.02;
	for (int i = 0; i < count; i++) {
		const double t = double(i) / rate;
		// Pitch: an exponential slide from f0 to f1, a jump at change_time, vibrato.
		double f = f0 * Math::pow(f1 / f0, t / total);
		if (change_time > 0.0 && t >= change_time) {
			f *= change_ratio;
		}
		if (vibrato_depth != 0.0) {
			f *= Math::pow(2.0, vibrato_depth / 12.0 * Math::sin(Math::TAU * vibrato_speed * t));
		}
		phase += f / rate;
		if (phase >= 1.0) {
			phase -= Math::floor(phase);
			noise_value = rng.randf() * 2.0 - 1.0; // Noise changes once per period: its pitch follows f.
		}
		double v = 0.0;
		switch (wave) {
			case 0:
				v = phase < CLAMP(duty + duty_sweep * t, 0.05, 0.95) ? 1.0 : -1.0;
				break;
			case 1:
				v = phase * 2.0 - 1.0;
				break;
			case 2:
				v = Math::sin(Math::TAU * phase);
				break;
			case 3:
				v = 1.0 - 4.0 * Math::abs(phase - 0.5);
				break;
			default:
				v = noise_value;
				break;
		}
		filtered += (v - filtered) * cutoff;
		// Envelope: rise, hold (with a punch that fades), fall to silence.
		double env;
		if (t < attack) {
			env = t / attack;
		} else if (t < attack + sustain) {
			env = 1.0 + punch * (1.0 - (t - attack) / MAX(sustain, 1e-6));
		} else {
			env = decay > 0.0 ? 1.0 - (t - attack - sustain) / decay : 0.0;
		}
		const double s = CLAMP(filtered * env * volume, -1.0, 1.0);
		const int16_t q = int16_t(Math::round(s * 32767.0));
		w[i * 2] = uint8_t(q & 0xff);
		w[i * 2 + 1] = uint8_t((q >> 8) & 0xff);
	}

	fprintf(stderr, "SFX loop done\n");
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
	stream->set_mix_rate(rate);
	stream->set_stereo(false);
	stream->set_data(data);
	return stream;
}

Ref<AudioStreamWAV> UZUSfx::generate_preset(const String &p_name, int p_seed) {
	fprintf(stderr, "SFX gen_preset enter\n");
	const Dictionary params = get_preset(p_name, p_seed);
	fprintf(stderr, "SFX preset ok empty=%d\n", params.is_empty() ? 1 : 0);
	if (params.is_empty()) {
		return Ref<AudioStreamWAV>();
	}
	return generate(params);
}

Dictionary UZUSfx::describe(const Ref<AudioStreamWAV> &p_stream) {
	Dictionary result;
	ERR_FAIL_COND_V(p_stream.is_null(), result);
	ERR_FAIL_COND_V_MSG(p_stream->get_format() != AudioStreamWAV::FORMAT_16_BITS || p_stream->is_stereo(), result, "UZUSfx: describe() reads 16-bit mono streams.");
	const Vector<uint8_t> data = p_stream->get_data();
	const int count = data.size() / 2;
	const int rate = p_stream->get_mix_rate();
	ERR_FAIL_COND_V(count == 0 || rate <= 0, result);
	auto sample = [&](int i) {
		return double(int16_t(uint16_t(data[i * 2]) | (uint16_t(data[i * 2 + 1]) << 8))) / 32767.0;
	};
	double peak = 0.0;
	double sum = 0.0;
	for (int i = 0; i < count; i++) {
		const double s = sample(i);
		peak = MAX(peak, Math::abs(s));
		sum += s * s;
	}
	// Zero crossings per second in a part of the sound (about twice the pitch of a tone).
	auto crossings = [&](int p_from, int p_to) {
		int n = 0;
		for (int i = p_from + 1; i < p_to; i++) {
			n += (sample(i - 1) < 0.0) != (sample(i) < 0.0);
		}
		return double(n) * rate / MAX(1, p_to - p_from);
	};
	const int fifth = MAX(2, count / 5);
	double tail = 0.0;
	for (int i = MAX(0, count - MAX(1, rate / 1000)); i < count; i++) { // The last millisecond: no click at the end.
		tail = MAX(tail, Math::abs(sample(i)));
	}
	result["length"] = double(count) / rate;
	result["peak"] = peak;
	result["rms"] = Math::sqrt(sum / count);
	result["pitch_start"] = crossings(0, MIN(count, fifth));
	result["pitch_end"] = crossings(MAX(0, count - fifth), count);
	result["ends_silent"] = tail < 0.05 * MAX(peak, 1e-6) + 1e-4;
	return result;
}

AudioStreamPlayer *UZUSfx::play(Node *p_parent, const Ref<AudioStream> &p_stream, float p_pitch_jitter, float p_volume_jitter_db, float p_volume_db) {
	ERR_FAIL_NULL_V(p_parent, nullptr);
	ERR_FAIL_COND_V(p_stream.is_null(), nullptr);
	AudioStreamPlayer *player = memnew(AudioStreamPlayer);
	player->set_name("UZUSfx");
	player->set_stream(p_stream);
	const float pitch_jitter = CLAMP(p_pitch_jitter, 0.0f, 0.9f);
	player->set_pitch_scale(1.0f + (Math::randf() * 2.0f - 1.0f) * pitch_jitter);
	player->set_volume_db(p_volume_db + (Math::randf() * 2.0f - 1.0f) * MAX(p_volume_jitter_db, 0.0f));
	player->set_autoplay(true); // Starts when it enters the tree.
	player->connect(SceneStringName(finished), callable_mp(static_cast<Node *>(player), &Node::queue_free));
	p_parent->add_child(player, true);
	return player;
}

void UZUSfx::_bind_methods() {
	ClassDB::bind_static_method("UZUSfx", D_METHOD("get_defaults"), &UZUSfx::get_defaults);
	ClassDB::bind_static_method("UZUSfx", D_METHOD("get_preset_names"), &UZUSfx::get_preset_names);
	ClassDB::bind_static_method("UZUSfx", D_METHOD("get_preset", "name", "seed"), &UZUSfx::get_preset, DEFVAL(0));
	ClassDB::bind_static_method("UZUSfx", D_METHOD("generate", "params"), &UZUSfx::generate);
	ClassDB::bind_static_method("UZUSfx", D_METHOD("generate_preset", "name", "seed"), &UZUSfx::generate_preset, DEFVAL(0));
	ClassDB::bind_static_method("UZUSfx", D_METHOD("describe", "stream"), &UZUSfx::describe);
	ClassDB::bind_static_method("UZUSfx", D_METHOD("play", "parent", "stream", "pitch_jitter", "volume_jitter_db", "volume_db"), &UZUSfx::play, DEFVAL(0.05), DEFVAL(1.0), DEFVAL(0.0));
}
