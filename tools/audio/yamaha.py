#!/usr/bin/env python3
"""yamaha.py -- AICA (Yamaha) 4-bit ADPCM encoder and reference decoder.

Decoder model (AICA / Flycast): predictor starts at 0, step at 0x7F; nibble n (low nibble of each byte
first): pred += (step * (2*(n&7) + 1) >> 3) * sign; step = step * QS[n&7] >> 8, clamped to 0x7F..0x6000.
The encoder searches the nibble with the smallest error at every sample (greedy).
"""

QS = (0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266)
STEP_MIN, STEP_MAX = 0x7F, 0x6000


def _step(pred, step, n):
    mag = n & 7
    d = (step * (2 * mag + 1)) >> 3
    pred = pred - d if n & 8 else pred + d
    pred = 32767 if pred > 32767 else (-32768 if pred < -32768 else pred)
    step = (step * QS[mag]) >> 8
    step = STEP_MIN if step < STEP_MIN else (STEP_MAX if step > STEP_MAX else step)
    return pred, step


def encode(samples):
    """s16 samples -> bytes (ceil(n/2)); returns (bytes, states) where states[i] = (pred, step) before
    sample i is decoded (used by tests for loop-point checks)."""
    out = bytearray((len(samples) + 1) // 2)
    pred, step = 0, STEP_MIN
    for i, x in enumerate(samples):
        diff = x - pred
        sign = 8 if diff < 0 else 0
        a = -diff if diff < 0 else diff
        # (2m+1)*step/8 ~= a  ->  m ~= (8a/step - 1)/2
        m = ((a * 8) // step - 1) // 2
        m = 0 if m < 0 else (7 if m > 7 else m)
        best_n, best_e, best = 0, None, None
        for c in (m - 1, m, m + 1):
            if c < 0 or c > 7:
                continue
            p2, s2 = _step(pred, step, sign | c)
            e = abs(x - p2)
            if best_e is None or e < best_e:
                best_n, best_e, best = sign | c, e, (p2, s2)
        pred, step = best
        if i & 1:
            out[i >> 1] |= best_n << 4
        else:
            out[i >> 1] = best_n
    return bytes(out)


def decode(data, nsamples):
    out = []
    pred, step = 0, STEP_MIN
    for i in range(nsamples):
        b = data[i >> 1]
        n = (b >> 4) if i & 1 else (b & 0xF)
        pred, step = _step(pred, step, n)
        out.append(pred)
    return out
