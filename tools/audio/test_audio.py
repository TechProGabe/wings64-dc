#!/usr/bin/env python3
"""test_audio.py -- host tests for the audio transcoder (docs/audio-design.md section 7).

ROM-free (CI): Yamaha ADPCM round trip SNR on a sine and a chirp, VADPCM decode with a zero codebook,
decimation filter DC gain, .aud table packing. With baserom.z64 present (local only): every ROM wave's
VADPCM decode reproduces its ALADPCMloop state (bit-exact oracle), and the built pool decodes with
SNR regression guards (worst >= 10 dB, median >= 22 dB) against the source PCM.
Usage: python3 -I tools/audio/test_audio.py [--rom]
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vadpcm  # noqa: E402
import yamaha  # noqa: E402
import transcode  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def snr_db(ref, got):
    sig = sum(x * x for x in ref) or 1
    err = sum((a - b) ** 2 for a, b in zip(ref, got)) or 1
    return 10 * math.log10(sig / err)


def test_yamaha():
    sine = [int(12000 * math.sin(2 * math.pi * 440 * i / 22050)) for i in range(22050)]
    d = yamaha.decode(yamaha.encode(sine), len(sine))
    s = snr_db(sine, d)
    check(s >= 25, f"yamaha sine 440 Hz SNR {s:.1f} dB >= 25")
    chirp = [int(10000 * math.sin(2 * math.pi * (100 + 4000 * i / 22050) * i / 22050)) for i in range(22050)]
    d = yamaha.decode(yamaha.encode(chirp), len(chirp))
    s = snr_db(chirp, d)
    check(s >= 15, f"yamaha chirp SNR {s:.1f} dB >= 15")
    check(len(yamaha.encode([0] * 7)) == 4, "yamaha packs ceil(n/2) bytes")
    enc = yamaha.encode([30000, -30000] * 50)
    check(all(-32768 <= v <= 32767 for v in yamaha.decode(enc, 100)), "yamaha decode saturates to s16")


def test_vadpcm():
    order, npred = 2, 1
    book = [0] * (order * npred * 8)
    frame = bytes([0x10]) + bytes([0x12, 0x34, 0x56, 0x70, 0x9A, 0xBC, 0xDE, 0xF0])
    out = vadpcm.decode(frame, order, npred, book)
    want = []
    for c in frame[1:]:
        for n in (c >> 4, c & 0xF):
            want.append((n - 16 if n >= 8 else n) * 2)
    check(out == want, "vadpcm zero codebook returns the scaled residuals")


def test_decimate():
    taps = transcode.lowpass_taps()
    check(abs(sum(taps) - 1.0) < 1e-9, "decimation filter has unity DC gain")
    dc = transcode.decimate([1000] * 200, 200, None)
    check(all(abs(v - 1000) <= 1 for v in dc[20:90]), "decimation keeps DC level")


def test_rom():
    sfx, music = transcode.load_rom_waves()
    bad = 0
    for w in sfx + music:
        if not w["loop"]:
            continue
        # The loop state is the 16 samples of the frame holding loopstart (see vadpcm.py).
        ctl_state = w.get("loop_state")
        n = len(w["data"]) // 9 * 16
        pcm = vadpcm.decode(w["data"], w["order"], w["npred"], w["book"], n)
        end = (w["loop"][0] // 16 + 1) * 16
        if ctl_state is not None and pcm[end - 16:end] != ctl_state:
            bad += 1
    check(bad == 0, f"vadpcm reproduces every ALADPCMloop state ({bad} mismatches)")
    worst, snrs = (99.0, None), []
    for w in sfx + music:
        r = transcode.transcode_one((w, transcode.FMT_ADPCM, True))
        if r["decimated"]:
            continue
        n = len(w["data"]) // 9 * 16
        pcm = vadpcm.decode(w["data"], w["order"], w["npred"], w["book"], n)[:r["samples"]]
        got = yamaha.decode(r["data"], r["samples"])
        s = snr_db(pcm, got)
        snrs.append(s)
        if s < worst[0]:
            worst = (s, w["base"])
        if r["samples"] > transcode.MAX_SAMPLES:
            check(False, f"wave 0x{w['base']:x} within the channel limit")
    # Saw-like waves with hard edges sit near 10 dB: Yamaha ADPCM's step adaptation lags the jumps and a
    # beam search buys ~1 dB. These are regression guards; listening on hardware decides pcm_force.txt.
    snrs.sort()
    check(worst[0] >= 10, f"worst ROM wave Yamaha SNR {worst[0]:.1f} dB (0x{worst[1] or 0:x}) >= 10")
    med = snrs[len(snrs) // 2]
    check(med >= 22, f"median ROM wave Yamaha SNR {med:.1f} dB >= 22 ({sum(1 for v in snrs if v < 20)} waves < 20 dB)")


def main():
    test_yamaha()
    test_vadpcm()
    test_decimate()
    if "--rom" in sys.argv:
        test_rom()
    print(f"test_audio: {'PASS' if not FAILS else 'FAIL (%d)' % len(FAILS)}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
