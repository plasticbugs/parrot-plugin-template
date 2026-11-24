#!/usr/bin/env python3
"""wavlevel.py [--span REF.wav] [--plain] [--check] FILE.wav ... -- how loud a
recording is, to compare a plugin's sound with an ordinary ColecoVision
cartridge's or with the arcade's (docs/testing.md, "How loud").

For each file it prints, in dBFS (0 is the WAV's full scale):

    rms      the level over its seconds with sound
    median   the median of those seconds' levels
    loudest  the loudest second
    peak     the highest sample (each second's mean taken out)

A second's level is its power from 20 Hz to 15 kHz, through a Hann
window. Two things in a recording of the cart's lists aren't sound and
would otherwise count: the DAC's resting level drifting back at a few Hz
after a loud sound, and the parked tones' 112 kHz carrier, which MAME's
resampling folds down to a 16-18 kHz tone at about -40 dBFS. A second
below -60 dBFS is silent.

--span REF.wav  measure each FILE only over the seconds where REF has
                sound, after lining the two up by their envelopes (up to
                3 s apart). Use the arcade's or the cartridge's recording
                of the same play as REF. What FILE has in REF's silent
                seconds is printed apart.
--plain         the old measure: the whole band, each second's mean taken
                out, no window (numbers from before this one, such as the
                cartridges in docs/testing.md, were measured this way).
--check         also say OK or FAIL for each file against the cartridges
                in docs/testing.md (-20.5 to -23.9 dBFS rms, about -22):
                louder than -20.5 fails, and so does quieter than -26 or a
                peak at full scale (clipping). Exits 1 on a FAIL.

Record the sounds the same way, so the numbers compare: a cartridge or
the arcade with MAME's -wavwrite, and the plugin's lists with
MAME_WAV=plugin.wav tools/mame-replay.sh. The simulator's --wav has its own
mixer, so don't compare it with MAME's. The WAV is 16-bit PCM, mono or
stereo (the channels are averaged). Python 3 alone does it; with numpy
it's quicker.

    python3 tools/wavlevel.py cart.wav plugin.wav
    python3 tools/wavlevel.py --span arcade.wav plugin.wav
"""
import cmath
import math
import struct
import sys
import wave

SILENT_DB = -60.0
BAND = (20.0, 15000.0)
HANN_MS = 0.375                 # a Hann window's mean square

try:
    import numpy as np          # optional: the same numbers, faster
except ImportError:
    np = None


def samples(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: not 16-bit PCM")
        ch, rate = w.getnchannels(), w.getframerate()
        data = w.readframes(w.getnframes())
    v = struct.unpack(f"<{len(data) // 2}h", data)
    if ch > 1:
        v = [sum(v[i:i + ch]) / ch for i in range(0, len(v) - ch + 1, ch)]
    return rate, list(v)


def db_power(ms):
    return 10 * math.log10(ms / 32768 ** 2) if ms > 0 else float("-inf")


# ---- a second's power in the band ----------------------------------------------------
def fft(x):
    """in place, radix 2, len(x) a power of 2"""
    n = len(x)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        half = size // 2
        w = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                a = x[start + k]
                b = x[start + k + half] * w[k]
                x[start + k] = a + b
                x[start + k + half] = a - b
        size *= 2
    return x


_hann = {}


def hann(n):
    if n not in _hann:
        _hann[n] = [0.5 - 0.5 * math.cos(2 * math.pi * (i + 0.5) / n) for i in range(n)]
    return _hann[n]


def band_ms(parts, rate):
    """each part's mean square from 20 Hz to 15 kHz (Hann-windowed;
    Parseval over both sides of the spectrum, the window's mean square)"""
    out = []
    if np is not None:
        for a in parts:
            n = len(a)
            x = np.asarray(a, dtype=float) * np.asarray(hann(n))
            X = np.fft.rfft(x)
            f = np.fft.rfftfreq(n, 1.0 / rate)
            sel = (f >= BAND[0]) & (f <= BAND[1])
            out.append(2 * float(np.sum(np.abs(X[sel]) ** 2)) / n / n / HANN_MS)
        return out
    # two seconds in one complex FFT (one real, one imaginary)
    for k in range(0, len(parts), 2):
        a = parts[k]
        b = parts[k + 1] if k + 1 < len(parts) else [0.0] * len(a)
        n = 1
        while n < max(len(a), len(b)):
            n *= 2
        wa, wb = hann(len(a)), hann(len(b))
        x = [complex(a[i] * wa[i] if i < len(a) else 0.0, b[i] * wb[i] if i < len(b) else 0.0) for i in range(n)]
        X = fft(x)
        lo, hi = int(BAND[0] * n / rate) + 1, int(BAND[1] * n / rate)
        pa = pb = 0.0
        for j in range(lo, hi + 1):
            xk, xm = X[j], X[n - j].conjugate()
            pa += abs(xk + xm) ** 2 / 4
            pb += abs(xk - xm) ** 2 / 4
        out.append(2 * pa / n / len(a) / HANN_MS)
        if k + 1 < len(parts):
            out.append(2 * pb / n / len(b) / HANN_MS)
    return out


def plain_ms(part):
    m = sum(part) / len(part)
    return sum((s - m) ** 2 for s in part) / len(part)


def seconds(rate, v, plain):
    """[(mean square, peak)] for each second (a last part shorter than half a second left out)"""
    parts = [v[i:i + rate] for i in range(0, len(v), rate)]
    if parts and len(parts[-1]) < rate // 2:
        parts.pop()
    ms = [plain_ms(p) for p in parts] if plain else band_ms(parts, rate)
    peaks = []
    for p in parts:
        m = sum(p) / len(p)
        peaks.append(max(abs(s - m) for s in p))
    return list(zip(ms, peaks))


def summary(secs):
    loud = [s for s in secs if db_power(s[0]) > SILENT_DB]
    top = max((s[1] for s in secs), default=0)
    peak = 20 * math.log10(top / 32768) if top else float("-inf")
    if not loud:
        return {"rms": float("-inf"), "median": float("-inf"), "loudest": float("-inf"), "peak": peak, "s": 0}
    levels = sorted(db_power(s[0]) for s in loud)
    return {"rms": db_power(sum(s[0] for s in loud) / len(loud)), "median": levels[len(levels) // 2],
            "loudest": levels[-1], "peak": peak, "s": len(loud)}


# --check: louder than the loudest cartridge measured, more than 4 dB under
# their average, or a peak at full scale
CHECK_LOUDEST, CHECK_QUIETEST, CLIP_DB = -20.5, -26.0, -0.1


def check(name, r):
    """OK or FAIL for a summary() against the cartridges; True if OK"""
    why = []
    if r["rms"] > CHECK_LOUDEST:
        why.append(f"louder than any cartridge ({CHECK_LOUDEST} dBFS)")
    if r["rms"] < CHECK_QUIETEST:
        why.append(f"quieter than {CHECK_QUIETEST} dBFS, 4 dB under a cartridge")
    if r.get("peak") is not None and r["peak"] >= CLIP_DB:
        why.append("a peak at full scale: it clips")
    print(f"{name}: {'FAIL, ' + '; '.join(why) if why else 'OK'} (cartridges: -20.5 to -23.9 dBFS rms)")
    return not why


def show(name, r, what="with sound"):
    print(f"{name}: rms {r['rms']:.1f}  median {r['median']:.1f}  loudest {r['loudest']:.1f}  "
          f"peak {r['peak']:.1f} dBFS  ({r['s']} seconds {what})")


# ---- lining two recordings up ----------------------------------------------------------
def envelope(x, rate, ms=10):
    n = int(rate * ms / 1000)
    out = []
    for i in range(0, len(x) - n + 1, n):
        w = x[i:i + n]
        m = sum(w) / n
        out.append(math.sqrt(sum((s - m) ** 2 for s in w) / n))
    return out, n


def offset(a, b, rate, most_s=3.0):
    """how many samples later b's sound is than a's (negative: earlier), and how well they match"""
    ea, step = envelope(a[:rate * 40], rate)
    eb, _ = envelope(b[:rate * 40], rate)
    best = (-2.0, 0)
    span = int(most_s * 100)
    for off in range(-span, span + 1):
        pairs = [(ea[i], eb[i + off]) for i in range(len(ea)) if 0 <= i + off < len(eb)]
        if len(pairs) < 100:
            continue
        ma = sum(p[0] for p in pairs) / len(pairs)
        mb = sum(p[1] for p in pairs) / len(pairs)
        sab = sum((p[0] - ma) * (p[1] - mb) for p in pairs)
        saa = sum((p[0] - ma) ** 2 for p in pairs)
        sbb = sum((p[1] - mb) ** 2 for p in pairs)
        c = sab / math.sqrt(saa * sbb) if saa and sbb else 0
        if c > best[0]:
            best = (c, off)
    return best[1] * step, best[0]


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    plain = "--plain" in args
    want_check = "--check" in args
    args = [a for a in args if a not in ("--plain", "--check")]
    ok = True
    ref = None
    if args and args[0] == "--span":
        if len(args) < 3:
            sys.exit(__doc__)
        ref, args = args[1], args[2:]
    if ref is None:
        for path in args:
            rate, v = samples(path)
            if not v:
                print(f"{path}: empty")
                continue
            r = summary(seconds(rate, v, plain))
            show(path, r)
            if want_check:
                ok = check(path, r) and ok
        sys.exit(0 if ok else 1)
    rr, rv = samples(ref)
    rsec = seconds(rr, rv, plain)
    show(ref, summary(rsec))
    for path in args:
        rate, v = samples(path)
        if rate != rr:
            sys.exit(f"{path}: {rate} samples a second, {ref} has {rr}: record both the same way")
        off, corr = offset(rv, v, rate)
        print(f"{path}: {off / rate:+.3f} s from {ref} (envelopes correlate {corr:.2f})")
        v = [0] * -off + v if off < 0 else v[off:]
        sec = seconds(rate, v, plain)
        n = min(len(sec), len(rsec))
        span = [sec[k] for k in range(n) if db_power(rsec[k][0]) > SILENT_DB]
        rest = [sec[k] for k in range(n) if db_power(rsec[k][0]) <= SILENT_DB]
        r = summary(span)
        r["s"] = len(span)
        show(f"  over {ref}'s seconds with sound", r, "of them")
        if want_check:
            ok = check(f"  {path}", r) and ok
        if rest:
            print(f"  in {ref}'s {len(rest)} silent seconds: {db_power(sum(s[0] for s in rest) / len(rest)):.1f} dBFS rms")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
