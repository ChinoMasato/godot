import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

import audio_check  # noqa: E402

RATE = 44100


def write_wav(path, samples, rate=RATE):
    with wave.open(str(path), "wb") as handle:
        handle.setparams((1, 2, rate, len(samples), "NONE", ""))
        handle.writeframes(struct.pack("<%dh" % len(samples), *samples))


def burst(seconds=0.17, freq=440.0, amp=0.6, rate=RATE):
    count = int(seconds * rate)
    return [int(amp * 32767 * math.sin(2 * math.pi * freq * i / rate) * math.exp(-i / rate / 0.05)) for i in range(count)]


def cosine_loop(envelope=None, rate=RATE):
    count = rate
    out = []
    for i in range(count):
        gain = envelope(i / count) if envelope else 1.0
        out.append(int(0.8 * 32767 * math.cos(2 * math.pi * 100 * i / rate) * gain))
    return out


class AudioCheckTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)

    def check(self, name, samples, **kwargs):
        path = self.root / name
        write_wav(path, samples)
        return audio_check.check_file(str(path), **kwargs)

    def test_normal_burst_passes(self):
        report = self.check("burst.wav", burst())
        self.assertTrue(report["ok"], report)
        self.assertTrue(report["checks"]["levels"]["pass"])
        self.assertTrue(report["checks"]["clipping"]["pass"])
        self.assertTrue(report["checks"]["silence"]["pass"])
        self.assertEqual(report["format"]["sample_rate"], RATE)
        self.assertAlmostEqual(report["checks"]["levels"]["peak"], 0.6, delta=0.02)

    def test_clipped_variant_fails(self):
        samples = [max(-32768, min(32767, v * 4)) for v in burst(amp=0.725, freq=660.0)]
        report = self.check("clipped.wav", samples)
        self.assertFalse(report["ok"])
        clipping = report["checks"]["clipping"]
        self.assertFalse(clipping["pass"])
        self.assertGreater(clipping["clip_ratio"], 0.1)

    def test_clip_threshold_override(self):
        samples = [max(-32768, min(32767, v * 4)) for v in burst(amp=0.725, freq=660.0)]
        report = self.check("clipped.wav", samples, max_clip_ratio=0.9)
        self.assertTrue(report["checks"]["clipping"]["pass"])

    def test_internal_silence_fails(self):
        one = burst()
        samples = one + [0] * (RATE * 3 // 10) + one
        report = self.check("gap.wav", samples)
        self.assertFalse(report["ok"])
        silence = report["checks"]["silence"]
        self.assertFalse(silence["pass"])
        self.assertGreater(silence["longest_run_ms"], 250.0)
        self.assertTrue(any(run["position"] == "middle" for run in silence["runs"]))

    def test_all_silent_file_fails(self):
        report = self.check("silent.wav", [0] * (RATE // 2))
        self.assertFalse(report["ok"])
        self.assertFalse(report["checks"]["levels"]["pass"])

    def test_seamless_loop_passes(self):
        report = self.check("loop.wav", cosine_loop(), loop=True)
        self.assertTrue(report["checks"]["loop_seam"]["pass"], report["checks"]["loop_seam"])

    def test_stepped_loop_fails(self):
        # A fade-in baked into the loop: the tail no longer meets the head.
        samples = cosine_loop(envelope=lambda t: 0.2 + 0.8 * t)
        report = self.check("loop.wav", samples, loop=True)
        seam = report["checks"]["loop_seam"]
        self.assertFalse(seam["pass"])
        self.assertGreater(seam["seam_jump"], 0.5)

    def test_cut_tail_loop_fails(self):
        samples = cosine_loop()
        samples[-200:] = [0] * 200
        report = self.check("loop.wav", samples, loop=True)
        self.assertFalse(report["checks"]["loop_seam"]["pass"])

    def make_sprite(self, shift=0, gap_samples=2205):
        first, second = burst(), burst(freq=880.0)
        gap = [0] * 2205
        samples = first + gap + second
        segments = {
            "first": {"start_sample": 0, "length_samples": len(first), "end_sample": len(first)},
            "second": {
                "start_sample": len(first) + 2205 + shift,
                "length_samples": len(second),
                "end_sample": len(first) + 2205 + shift + len(second),
            },
        }
        manifest = {
            "names": ["first", "second"],
            "gap_ms": 50,
            "seed": 0,
            "sample_rate": RATE,
            "gap_samples": gap_samples,
            "total_samples": len(samples),
            "total_sec": len(samples) / RATE,
            "segments": segments,
        }
        wav_path = self.root / "sprite.wav"
        manifest_path = self.root / "sprite.json"
        write_wav(wav_path, samples)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return str(wav_path), str(manifest_path)

    def test_sprite_passes(self):
        wav_path, manifest_path = self.make_sprite()
        report = audio_check.check_file(wav_path, manifest=manifest_path)
        self.assertTrue(report["ok"], report["checks"]["sprite"])
        self.assertTrue(report["checks"]["sprite"]["pass"])

    def test_sprite_shifted_manifest_fails(self):
        wav_path, manifest_path = self.make_sprite(shift=500)
        report = audio_check.check_file(wav_path, manifest=manifest_path)
        self.assertFalse(report["checks"]["sprite"]["pass"])
        self.assertTrue(any("second" in issue for issue in report["checks"]["sprite"]["issues"]))

    def test_sprite_wrong_gap_fails(self):
        wav_path, manifest_path = self.make_sprite(gap_samples=2000)
        report = audio_check.check_file(wav_path, manifest=manifest_path)
        self.assertFalse(report["checks"]["sprite"]["pass"])

    def test_missing_file_reports_error(self):
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "audio-check", str(self.root / "nope.wav")],
            capture_output=True,
            text=True,
        )
        self.assertEqual(process.returncode, 1)
        self.assertFalse(json.loads(process.stdout)["ok"])

    def test_cli_json_shape_and_exit_code(self):
        path = self.root / "burst.wav"
        write_wav(path, burst())
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "audio-check", str(path)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        report = json.loads(process.stdout)
        self.assertTrue(report["ok"])
        self.assertEqual(report["command"], "audio-check")
        self.assertIn("levels", report["checks"])
        self.assertIn("clipping", report["checks"])
        self.assertIn("silence", report["checks"])


if __name__ == "__main__":
    unittest.main()
