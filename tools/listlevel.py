#!/usr/bin/env python3
"""listlevel.py LISTS.bin [--ladder tools/ladder.json] -- how loud a frame
plugin's lists will sound, without MAME or the ColecoVision BIOS.

The MAME step (MAME_WAV=plugin.wav tools/mame-replay.sh, then
tools/wavlevel.py) needs the BIOS, coleco.zip, which the kit can't
include. This works the sound out from the lists themselves, as the
models record them (test/run.sh leaves LISTS.bin in each run's folder):
each sound byte at its time in the list by the kernel's costs, each
voice's level from tools/ladder.json (measured on a real console), and
the result scaled to what MAME's recording of the same lists measures.
It prints the same numbers as wavlevel.py, in the same dBFS, so a plugin
can be set as loud as a cartridge (docs/testing.md, "How loud").

It prints rms, median and loudest as wavlevel.py does (not the peak,
which this can't know). They agree with MAME's recording within about
half a dB, for samples through the three-tone DAC, tones and noise. Use
MAME when you have the BIOS, and a TV in the end.

Only frame-kernel lists carry their sound. A screen-kernel plugin's
sound isn't in LISTS.bin; its log.txt has it.

    python3 tools/listlevel.py "$TMPDIR/parrotcart-test/myplugin/lists.bin"
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wavlevel  # noqa: E402  (the same band, window and silence rule)

Z80_HZ = 3579545
FRAME_T = 262 * 228
NOWAIT_T, NOWAIT2_T, NMI_T = 354, 533, 105
START_T, START2_T = 480, 660
STEP_T = 64                       # an output sample every 64 cycles: 55,930 a second
# MAME's recording of the cart's lists, in dBFS, against the levels' units
# (the mean square of the voices' summed levels, 1.0 a voice at attenuation
# 0). Measured with MAME 0.288: examples/samples' notes and drum -18.5, a
# tone at attenuation 0 -18.1, at 6 -17.9, two tones -18.1, white noise
# -18.1
CAL_DB = -18.2


def lists(path):
    """(flags, controller2, bytes) for each frame list in the file"""
    out = []
    with open(path, "rb") as f:
        data = f.read()
    p = 0
    while p + 9 <= len(data):
        kind, full, n = data[p], data[p + 1], data[p + 3] | data[p + 4] << 8
        flags = data[p + 8]
        body = data[p + 9:p + 9 + n]
        p += 9 + n
        if kind == ord("F"):
            out.append((flags, full, body))
    return out


def events(recs):
    """the sound bytes, each with its time in cycles, on the console's clock:
    a list starts after the frame's NMI, or straight after the list before
    when that one ran past the next NMI (back to back)"""
    ev = []
    nmi_next = FRAME_T
    t0 = None
    for _flags, ctrl2, b in recs:
        if t0 is None:
            t0 = START2_T if ctrl2 else START_T
        i, t, vblank = 0, 0, True
        while i < len(b):
            op = b[i]
            if op == 1:
                n = b[i + 3]
                t += 106 + (23 if vblank else 31) * n
                i += 4 + n
            elif op == 2:
                n = b[i + 1]
                for k in range(n):
                    ev.append((t0 + t + 75 + 51 * k, b[i + 2 + k]))
                t += 75 + 51 * n
                i += 2 + n
            elif op == 3:
                t += 114
                i += 3
            elif op == 0 and vblank:
                vblank = False
                t += 54
                i += 1
            else:
                t += 52
                break
        end = t0 + t
        past = False
        while nmi_next <= end:
            end += NMI_T
            nmi_next += FRAME_T
            past = True
        t0 = end + (NOWAIT2_T if ctrl2 else NOWAIT_T) if past else nmi_next + (START2_T if ctrl2 else START_T)
        if not past:
            nmi_next += FRAME_T
    return ev, t0 or 0


def render(ev, end, amp):
    """the chip's output in the ladder's units, a sample every STEP_T cycles"""
    period = [1, 1, 1]
    att = [15, 15, 15, 15]
    latch = 0
    noise_ctl, lfsr, noise_bit, noise_count = 0, 0x4000, 0, 0
    phase = [0.0, 0.0, 0.0]
    out = []
    e = 0
    nev = len(ev)
    for t in range(0, end, STEP_T):
        while e < nev and ev[e][0] <= t:
            v = ev[e][1]
            e += 1
            if v & 0x80:
                latch = (v >> 4) & 7
                ch, reg = latch >> 1, latch & 1
                if reg:
                    att[ch] = v & 15
                elif ch < 3:
                    period[ch] = (period[ch] & 0x3F0) | (v & 15)
                else:
                    noise_ctl = v & 7
                    lfsr = 0x4000
            else:
                ch, reg = latch >> 1, latch & 1
                if reg:
                    att[ch] = v & 15
                elif ch < 3:
                    period[ch] = (period[ch] & 15) | (v & 0x3F) << 4
        s = 0.0
        for ch in range(3):
            a = amp[ch][att[ch]]
            if not a:
                continue
            p = period[ch] or 1024
            f = Z80_HZ / (32.0 * p)
            if f > 20000:               # parked, or above hearing: its mean
                s += a
            else:                       # a square from 0 to twice its mean
                phase[ch] = (phase[ch] + f * STEP_T / Z80_HZ) % 1.0
                s += 2 * a if phase[ch] < 0.5 else 0.0
        a = amp[2][att[3]]
        if a:
            rate = (16, 32, 64)[noise_ctl & 3] * 32 if (noise_ctl & 3) < 3 else 32 * (period[2] or 1024)
            noise_count += STEP_T
            while noise_count >= rate:
                noise_count -= rate
                fb = (lfsr ^ (lfsr >> 1)) & 1 if noise_ctl & 4 else lfsr & 1
                noise_bit = lfsr & 1
                lfsr = (lfsr >> 1) | (fb << 14)
            s += 2 * a if noise_bit else 0.0
        out.append(s)
    return out


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    ladder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ladder.json")
    if "--ladder" in args:
        i = args.index("--ladder")
        ladder = args[i + 1]
        del args[i:i + 2]
    amp = json.load(open(ladder))["amp"]
    for path in args:
        recs = lists(path)
        if not recs:
            print(f"{path}: no frame lists (a screen-kernel plugin's sound isn't in lists.bin)")
            continue
        ev, end = events(recs)
        x = render(ev, end, amp)
        rate = Z80_HZ / STEP_T
        # wavlevel's seconds and summary, on samples in the ladder's units
        # scaled so that 1.0 is CAL_DB's full scale
        scale = 32768 * 10 ** (CAL_DB / 20)
        v = [s * scale for s in x]
        r = int(round(rate))
        res = wavlevel.summary(wavlevel.seconds(r, v, False))
        print(f"{path}: rms {res['rms']:.1f}  median {res['median']:.1f}  loudest {res['loudest']:.1f} dBFS  "
              f"({res['s']} seconds with sound, of {end / Z80_HZ:.1f}; {len(recs)} lists)")


if __name__ == "__main__":
    main()
