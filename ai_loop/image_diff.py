#!/usr/bin/env python3
"""Compare two PNG screenshots (from ai_loop/screenshot.sh) with a tolerance.

Usage: ai_loop/image_diff.py <expected.png> <actual.png> [--max-mean 2.0] [--max-changed 1.0]

Prints the mean absolute channel difference (0-255) and the share of pixels whose
largest channel difference exceeds 16. Exits 0 when both are within limits, 1 when
the images differ, 2 on bad input. Standard library only (8-bit RGB/RGBA PNGs,
as written by Movie Maker), so it runs where Pillow is not installed.
"""

import argparse
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat = 8, b""
    width = height = channels = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in (2, 6) or interlace != 0:
                raise ValueError(f"{path}: only 8-bit non-interlaced RGB/RGBA PNGs are supported")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if width is None:
        raise ValueError(f"{path}: missing IHDR")
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        filt = raw[i]
        line = bytearray(raw[i + 1 : i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if filt == 1:
                line[x] = (line[x] + a) & 0xFF
            elif filt == 2:
                line[x] = (line[x] + b) & 0xFF
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 0xFF
        rows.append(line)
        prev = line
    return width, height, channels, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("expected")
    parser.add_argument("actual")
    parser.add_argument("--max-mean", type=float, default=2.0, help="max mean absolute channel difference (0-255)")
    parser.add_argument("--max-changed", type=float, default=1.0, help="max percent of pixels that changed visibly")
    args = parser.parse_args()
    try:
        w1, h1, c1, r1 = read_png(args.expected)
        w2, h2, c2, r2 = read_png(args.actual)
    except (OSError, ValueError, zlib.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if (w1, h1) != (w2, h2):
        print(f"size differs: {w1}x{h1} vs {w2}x{h2}")
        return 1
    total = changed = 0
    for y in range(h1):
        a, b = r1[y], r2[y]
        for x in range(w1):
            worst = 0
            for k in range(3):
                d = abs(a[x * c1 + k] - b[x * c2 + k])
                total += d
                worst = max(worst, d)
            if worst > 16:
                changed += 1
    pixels = w1 * h1
    mean = total / (pixels * 3)
    pct = 100.0 * changed / pixels
    ok = mean <= args.max_mean and pct <= args.max_changed
    print(f"mean_abs_diff={mean:.3f} changed_pixels={pct:.3f}% -> {'same' if ok else 'different'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
