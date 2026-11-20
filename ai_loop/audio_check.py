"""Mechanical checks for game audio files (BACKLOG B-1111).

An AI agent cannot listen. What it can do is measure the waveform, so this
tool inspects a WAV file numerically and returns one JSON report:

  levels    peak / RMS loudness (and the all-silent file, which fails)
  clipping  how many samples sit at full scale, and the longest clipped run
  silence   runs below a threshold; a run longer than the limit fails,
            wherever it sits (head, middle or tail)
  loop_seam (only with --loop) the waveform step across the loop point,
            judged against the file's own sample-to-sample steps
  sprite    (only with --manifest) an audio sprite (B-1108) against its
            manifest: sample rate and total length, segment bounds and
            order, every segment actually containing sound, and every gap
            (plus head / tail) being silence of exactly the declared length

Simultaneous-playback overlap (too many voices at once) is out of scope:
it needs playback-side logs, not a waveform. Nothing here judges how a
sound *sounds*; the report is numbers only, and every threshold is a flag
so a project can tighten or loosen the defaults. The defaults are set from
measurements of the UZUSfx presets (peaks 0.49..0.73, zero clipped
samples, longest -60 dB silence run 6 ms), with wide margins.

WAV reading uses only the standard library (the wave module); PCM
8/16/24/32-bit, mono or multichannel. Compressed WAV is rejected.
"""

import argparse
import json
import math
import sys
import wave
from array import array

DEFAULT_SILENCE_DB = -60.0
DEFAULT_MAX_SILENCE_MS = 100.0
DEFAULT_CLIP_LEVEL = 0.999
DEFAULT_MAX_CLIP_RATIO = 0.001
DEFAULT_MAX_SEAM_JUMP = 0.05
DEFAULT_SEAM_RATIO = 4.0

# Silence runs shorter than this are counted but not listed one by one.
REPORT_RUN_MS = 5.0
MAX_REPORTED_RUNS = 16


class AudioCheckError(Exception):
    pass


def _db(value):
    return 20.0 * math.log10(value) if value > 0.0 else -120.0


def read_wav(path):
    """Return {rate, channels, bits, frames, samples} with samples a
    float array of interleaved values normalized to [-1, 1)."""
    try:
        handle = wave.open(path, "rb")
    except (OSError, EOFError, wave.Error) as error:
        raise AudioCheckError(f"cannot read {path}: {error}") from error
    with handle:
        if handle.getcomptype() != "NONE":
            raise AudioCheckError(f"{path}: compressed WAV ({handle.getcomptype()}) is not supported")
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    samples = array("f")
    if width == 1:  # 8-bit PCM is unsigned.
        samples = array("f", ((b - 128) / 128.0 for b in raw))
    elif width == 2:
        ints = array("h")
        ints.frombytes(raw)
        if sys.byteorder == "big":
            ints.byteswap()
        samples = array("f", (v / 32768.0 for v in ints))
    elif width == 3:
        values = []
        for i in range(0, len(raw) - 2, 3):
            v = raw[i] | (raw[i + 1] << 8) | (raw[i + 2] << 16)
            if v >= 0x800000:
                v -= 0x1000000
            values.append(v / 8388608.0)
        samples = array("f", values)
    elif width == 4:
        ints = array("i")
        ints.frombytes(raw)
        if sys.byteorder == "big":
            ints.byteswap()
        samples = array("f", (v / 2147483648.0 for v in ints))
    else:
        raise AudioCheckError(f"{path}: unsupported sample width {width} bytes")
    return {"rate": rate, "channels": channels, "bits": width * 8, "frames": frames, "samples": samples}


def frame_peaks(samples, channels):
    """Per-frame peak across channels, as a float array."""
    if channels == 1:
        return array("f", (abs(v) for v in samples))
    peaks = array("f", bytes(4 * (len(samples) // channels)))
    for i, v in enumerate(samples):
        frame = i // channels
        a = abs(v)
        if a > peaks[frame]:
            peaks[frame] = a
    return peaks


def silence_runs(peaks, threshold):
    """Maximal runs of frames with peak < threshold: [(start, length)]."""
    runs = []
    start = None
    for i, v in enumerate(peaks):
        if v < threshold:
            if start is None:
                start = i
        elif start is not None:
            runs.append((start, i - start))
            start = None
    if start is not None:
        runs.append((start, len(peaks) - start))
    return runs


def check_levels(samples):
    peak = 0.0
    square_sum = 0.0
    for v in samples:
        a = abs(v)
        if a > peak:
            peak = a
        square_sum += v * v
    rms = math.sqrt(square_sum / len(samples)) if samples else 0.0
    return {
        "pass": peak > 0.0,
        "peak": round(peak, 6),
        "peak_db": round(_db(peak), 2),
        "rms": round(rms, 6),
        "rms_db": round(_db(rms), 2),
        "issue": None if peak > 0.0 else "the file is entirely silent (peak is 0)",
    }


def check_clipping(samples, clip_level, max_ratio):
    clipped = 0
    longest = 0
    run = 0
    for v in samples:
        if abs(v) >= clip_level:
            clipped += 1
            run += 1
            if run > longest:
                longest = run
        else:
            run = 0
    ratio = clipped / len(samples) if samples else 0.0
    return {
        "pass": ratio <= max_ratio,
        "clip_level": clip_level,
        "clipped_samples": clipped,
        "clip_ratio": round(ratio, 6),
        "max_clip_ratio": max_ratio,
        "longest_clip_run": longest,
        "issue": None if ratio <= max_ratio else f"{ratio:.4%} of samples are at full scale (limit {max_ratio:.4%})",
    }


def check_silence(peaks, rate, silence_db, max_silence_ms):
    threshold = 10.0 ** (silence_db / 20.0)
    runs = silence_runs(peaks, threshold)
    total = len(peaks)
    reported = []
    longest_ms = 0.0
    ok = True
    for start, length in runs:
        ms = length * 1000.0 / rate
        longest_ms = max(longest_ms, ms)
        if ms > max_silence_ms:
            ok = False
        if ms >= REPORT_RUN_MS and len(reported) < MAX_REPORTED_RUNS:
            position = "head" if start == 0 else "tail" if start + length == total else "middle"
            reported.append(
                {
                    "start_sec": round(start / rate, 4),
                    "duration_ms": round(ms, 1),
                    "position": position,
                }
            )
    return {
        "pass": ok,
        "threshold_db": silence_db,
        "max_silence_ms": max_silence_ms,
        "longest_run_ms": round(longest_ms, 1),
        "runs": reported,
        "issue": None if ok else f"a silence run of {longest_ms:.0f} ms exceeds the {max_silence_ms:.0f} ms limit",
    }


def check_loop_seam(samples, channels, max_seam_jump, seam_ratio):
    """The step across the loop point, per channel, against the file's own
    sample-to-sample steps (a seamless loop's wrap step is an ordinary step)."""
    frames = len(samples) // channels
    if frames < 4:
        return {"pass": False, "issue": "file is too short to judge a loop seam"}
    worst_jump = 0.0
    worst_derivative = 0.0
    deltas = []
    for ch in range(channels):
        chan = samples[ch::channels]
        jump = abs(chan[0] - chan[-1])
        derivative = abs((chan[1] - chan[0]) - (chan[-1] - chan[-2]))
        worst_jump = max(worst_jump, jump)
        worst_derivative = max(worst_derivative, derivative)
        step = max(1, len(chan) // 200000)
        deltas.extend(abs(chan[i] - chan[i - 1]) for i in range(1, len(chan), step))
    deltas.sort()
    p999 = deltas[min(len(deltas) - 1, int(len(deltas) * 0.999))] if deltas else 0.0
    bound = max(max_seam_jump, seam_ratio * p999)
    ok = worst_jump <= bound
    return {
        "pass": ok,
        "seam_jump": round(worst_jump, 6),
        "seam_jump_db": round(_db(worst_jump), 2),
        "derivative_jump": round(worst_derivative, 6),
        "internal_step_p999": round(p999, 6),
        "allowed_jump": round(bound, 6),
        "issue": None if ok else f"the loop point steps by {worst_jump:.3f} (allowed {bound:.3f}); it will click",
    }


def _region_peak(peaks, start, end):
    peak = 0.0
    for i in range(max(0, start), min(len(peaks), end)):
        if peaks[i] > peak:
            peak = peaks[i]
    return peak


def check_sprite(peaks, rate, wav_frames, manifest_path, silence_db):
    threshold = 10.0 ** (silence_db / 20.0)
    try:
        with open(manifest_path, encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, ValueError) as error:
        return {"pass": False, "manifest": manifest_path, "issue": f"cannot read the manifest: {error}"}
    if not isinstance(manifest, dict):
        return {"pass": False, "manifest": manifest_path, "issue": "the manifest is not a JSON object"}
    segments = manifest.get("segments")
    if not isinstance(segments, dict):
        # The bare UZUSfx.sprite_manifest shape: {name: segment, ...}.
        if manifest and all(isinstance(v, dict) and "start_sample" in v for v in manifest.values()):
            segments = manifest
            manifest = {}
        else:
            return {"pass": False, "manifest": manifest_path, "issue": "the manifest has no 'segments' object"}
    issues = []
    if "sample_rate" in manifest and manifest["sample_rate"] != rate:
        issues.append(f"sample rate: manifest {manifest['sample_rate']} != WAV {rate}")
    if "total_samples" in manifest and manifest["total_samples"] != wav_frames:
        issues.append(f"total length: manifest {manifest['total_samples']} samples != WAV {wav_frames} frames")
    gap_expected = manifest.get("gap_samples")
    ordered = sorted(segments.items(), key=lambda item: item[1].get("start_sample", 0))
    segment_reports = []
    previous_end = 0
    for index, (name, segment) in enumerate(ordered):
        seg_issues = []
        start = int(segment.get("start_sample", -1))
        length = int(segment.get("length_samples", -1))
        end = int(segment.get("end_sample", start + length)) if start >= 0 and length >= 0 else -1
        if start < 0 or length <= 0 or end > wav_frames:
            seg_issues.append(f"segment bounds [{start}, {end}) do not fit the WAV's {wav_frames} frames")
            start, end = max(0, start), max(0, end)
        if index == 0 and start > 0 and _region_peak(peaks, 0, start) >= threshold:
            seg_issues.append(f"the head before the first segment is not silent (peak {_region_peak(peaks, 0, start):.4f})")
        if index > 0:
            gap = start - previous_end
            if gap < 0:
                seg_issues.append(f"segment overlaps the previous one by {-gap} samples")
            else:
                if gap_expected is not None and gap != gap_expected:
                    seg_issues.append(f"gap before this segment is {gap} samples, the manifest declares {gap_expected}")
                gap_peak = _region_peak(peaks, previous_end, start)
                if gap_peak >= threshold:
                    seg_issues.append(f"the gap before this segment is not silent (peak {gap_peak:.4f})")
        peak = _region_peak(peaks, start, end)
        if peak < threshold:
            seg_issues.append("the segment itself is silent")
        segment_reports.append({"name": name, "pass": not seg_issues, "peak": round(peak, 6), "issues": seg_issues})
        previous_end = max(previous_end, end)
    if ordered and previous_end < wav_frames and _region_peak(peaks, previous_end, wav_frames) >= threshold:
        issues.append(f"the tail after the last segment is not silent (peak {_region_peak(peaks, previous_end, wav_frames):.4f})")
    for report in segment_reports:
        issues.extend(f"{report['name']}: {issue}" for issue in report["issues"])
    return {
        "pass": not issues,
        "manifest": manifest_path,
        "segments": segment_reports,
        "issues": issues,
        "issue": None if not issues else "; ".join(issues),
    }


def check_file(path, manifest=None, loop=False, silence_db=DEFAULT_SILENCE_DB,
               max_silence_ms=DEFAULT_MAX_SILENCE_MS, clip_level=DEFAULT_CLIP_LEVEL,
               max_clip_ratio=DEFAULT_MAX_CLIP_RATIO, max_seam_jump=DEFAULT_MAX_SEAM_JUMP,
               seam_ratio=DEFAULT_SEAM_RATIO):
    info = read_wav(path)
    samples = info["samples"]
    peaks = frame_peaks(samples, info["channels"])
    checks = {
        "levels": check_levels(samples),
        "clipping": check_clipping(samples, clip_level, max_clip_ratio),
        "silence": check_silence(peaks, info["rate"], silence_db, max_silence_ms),
    }
    if loop:
        checks["loop_seam"] = check_loop_seam(samples, info["channels"], max_seam_jump, seam_ratio)
    if manifest:
        checks["sprite"] = check_sprite(peaks, info["rate"], info["frames"], manifest, silence_db)
    return {
        "ok": all(check["pass"] for check in checks.values()),
        "command": "audio-check",
        "file": path,
        "format": {
            "sample_rate": info["rate"],
            "channels": info["channels"],
            "bits": info["bits"],
            "frames": info["frames"],
            "duration_sec": round(info["frames"] / info["rate"], 4) if info["rate"] else 0.0,
        },
        "thresholds": {
            "silence_db": silence_db,
            "max_silence_ms": max_silence_ms,
            "clip_level": clip_level,
            "max_clip_ratio": max_clip_ratio,
            "max_seam_jump": max_seam_jump,
            "seam_ratio": seam_ratio,
        },
        "checks": checks,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(prog="uzu.py audio-check", description="Mechanical checks for a WAV file (B-1111): levels, clipping, silence, loop seam, sprite consistency.")
    parser.add_argument("file", help="the WAV file to inspect")
    parser.add_argument("--manifest", help="an audio-sprite manifest JSON (B-1108) to check the WAV against")
    parser.add_argument("--loop", action="store_true", help="the file is meant to loop: also check the seam")
    parser.add_argument("--silence-db", type=float, default=DEFAULT_SILENCE_DB, help=f"silence threshold in dBFS (default {DEFAULT_SILENCE_DB})")
    parser.add_argument("--max-silence-ms", type=float, default=DEFAULT_MAX_SILENCE_MS, help=f"longest tolerated silence run (default {DEFAULT_MAX_SILENCE_MS})")
    parser.add_argument("--clip-level", type=float, default=DEFAULT_CLIP_LEVEL, help=f"level counted as full scale (default {DEFAULT_CLIP_LEVEL})")
    parser.add_argument("--max-clip-ratio", type=float, default=DEFAULT_MAX_CLIP_RATIO, help=f"tolerated share of full-scale samples (default {DEFAULT_MAX_CLIP_RATIO})")
    parser.add_argument("--max-seam-jump", type=float, default=DEFAULT_MAX_SEAM_JUMP, help=f"absolute floor for the tolerated loop step (default {DEFAULT_MAX_SEAM_JUMP})")
    parser.add_argument("--seam-ratio", type=float, default=DEFAULT_SEAM_RATIO, help=f"the loop step may be this many times the file's own p99.9 step (default {DEFAULT_SEAM_RATIO})")
    parser.add_argument("--out", help="also write the report to this path, indented")
    args = parser.parse_args(argv)
    try:
        report = check_file(
            args.file,
            manifest=args.manifest,
            loop=args.loop,
            silence_db=args.silence_db,
            max_silence_ms=args.max_silence_ms,
            clip_level=args.clip_level,
            max_clip_ratio=args.max_clip_ratio,
            max_seam_jump=args.max_seam_jump,
            seam_ratio=args.seam_ratio,
        )
    except AudioCheckError as error:
        print(json.dumps({"ok": False, "command": "audio-check", "error": str(error)}, ensure_ascii=False))
        return 1
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
