#!/usr/bin/env python3
"""knee.py [--knee K] PART ... --gain G [G ...] -- what a soft knee does to
each part of a plugin's mix (porting.md, "How loud").

A loudness target can hide a change in the balance: the knee squeezes the
part with the big peaks (explosions, a phaser) and leaves the quiet one
(music, speech) as it was, so the mix reaches its rms with the music
louder against the effects than in the arcade. This says by how much.

Each PART is one part of the mix made alone, before the knee and at the
same gain: the plugin's sample stream with only the music, then only the
effects, and so on (the plugin's own test build writes them). Their sum is
the mix. A part is a 16-bit WAV, or raw 32-bit floats (.f32, native byte
order) with 1.0 as full scale. All must have the same rate and length.

For each gain G (a multiple of the parts as given), it prints: the share
of samples that go into the knee and past full scale, how much the knee
takes off the mix's rms, and how much it takes off each part (the knee's
output fitted as a sum of the parts, each with its own weight: 0 dB is
untouched).

The knee is the one in porting.md: straight up to K of full scale
(default 0.75), then y = K + (1 - K)(1 - exp(-(x - K) / (1 - K))), the sign
kept.

    python3 tools/knee.py music.f32 effects.f32 --gain 1 1.5 2
"""
import array
import math
import struct
import sys
import wave


def load(path):
    if path.endswith(".f32"):
        a = array.array("f")
        with open(path, "rb") as f:
            a.frombytes(f.read())
        return list(a)
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: a 16-bit WAV or .f32, please")
        ch, n = w.getnchannels(), w.getnframes()
        v = struct.unpack(f"<{n * ch}h", w.readframes(n))
    if ch > 1:
        v = [sum(v[i:i + ch]) / ch for i in range(0, len(v), ch)]
    return [s / 32768.0 for s in v]


def knee(x, k):
    ax = min(abs(x), 8.0)
    y = ax if ax <= k else k + (1 - k) * (1 - math.exp(-(ax - k) / (1 - k)))
    return -y if x < 0 else y


def solve(a, b):
    """a x = b, by Gaussian elimination (a few parts)"""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        if abs(m[c][c]) < 1e-30:
            return None
        for r in range(n):
            if r != c:
                f = m[r][c] / m[c][c]
                m[r] = [m[r][j] - f * m[c][j] for j in range(n + 1)]
    return [m[i][n] / m[i][i] for i in range(n)]


def db(x):
    return 20 * math.log10(x) if x > 0 else float("-inf")


def main():
    args = sys.argv[1:]
    if "--gain" not in args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    k = 0.75
    if "--knee" in args:
        i = args.index("--knee")
        k = float(args[i + 1])
        del args[i:i + 2]
    g = args.index("--gain")
    paths, gains = args[:g], [float(v) for v in args[g + 1:]]
    if not paths or not gains:
        sys.exit(__doc__)
    parts = [load(p) for p in paths]
    n = min(len(p) for p in parts)
    if not n:
        sys.exit("knee.py: a part is empty")
    print(f"{len(parts)} parts, {n} samples; the knee at {k:g} of full scale")
    for p, path in zip(parts, paths):
        print(f"  {path}: rms {db(math.sqrt(sum(v * v for v in p[:n]) / n)):.1f} dB of full scale")
    for gain in gains:
        into = over = 0
        sxx = syy = 0.0
        pp = [[0.0] * len(parts) for _ in parts]          # sum of part i * part j
        py = [0.0] * len(parts)                           # sum of part i * y
        for s in range(n):
            vals = [p[s] * gain for p in parts]
            x = sum(vals)
            y = knee(x, k)
            if abs(x) > k:
                into += 1
            if abs(x) > 1:
                over += 1
            sxx += x * x
            syy += y * y
            for i, vi in enumerate(vals):
                py[i] += vi * y
                for j in range(i, len(vals)):
                    pp[i][j] += vi * vals[j]
        for i in range(len(parts)):
            for j in range(i):
                pp[i][j] = pp[j][i]
        w = solve(pp, py)
        each = "  ".join(f"{paths[i]} {db(w[i]):+.2f} dB" if w and w[i] > 0 else f"{paths[i]} ?" for i in range(len(parts)))
        print(f"gain {gain:g}: {100 * into / n:.2f}% of samples into the knee, {100 * over / n:.2f}% past full scale; "
              f"the mix's rms {10 * math.log10(syy / sxx) if sxx else 0:+.2f} dB through it; {each}")


if __name__ == "__main__":
    main()
