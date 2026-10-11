#!/usr/bin/env python3
"""vadpcm.py -- N64 VADPCM decoder (libaudio AL_ADPCM_WAVE, 9-byte frames of 16 samples).

Follows the sm64 decomp's tools/aifc_decode.c (my_decodeframe + readaifccodebook): codebook rows are
expanded to order+8 coefficients, each output is a floor(/2048) inner product plus the scaled residual.
Outputs and carried state are saturated to s16 like the RSP's vector saturation.
"""


def expand_book(order, npredictors, book):
    """book: flat list of s16, npredictors * order * 8 entries in ROM order. Returns
    tables[p][k] = list of order+8 coefficients (aifc_decode.c readaifccodebook)."""
    tables = []
    for p in range(npredictors):
        t = [[0] * (order + 8) for _ in range(8)]
        for j in range(order):
            for k in range(8):
                t[k][j] = book[p * order * 8 + j * 8 + k]
        for k in range(1, 8):
            t[k][order] = t[k - 1][order - 1]
        t[0][order] = 1 << 11
        for k in range(1, 8):
            for j in range(8):
                t[j][k + order] = 0 if j < k else t[j - k][order]
        tables.append(t)
    return tables


def _sat(x):
    return 32767 if x > 32767 else (-32768 if x < -32768 else x)


def decode(data, order, npredictors, book, nsamples=None):
    """Decode VADPCM bytes to a list of s16 samples (16 per 9-byte frame)."""
    tables = expand_book(order, npredictors, book)
    nframes = len(data) // 9
    out = []
    state = [0] * 16
    for f in range(nframes):
        o = f * 9
        header = data[o]
        scale = 1 << (header >> 4)
        coefs = tables[header & 0xF] if (header & 0xF) < npredictors else tables[0]
        ix = [0] * 16
        for i in range(8):
            c = data[o + 1 + i]
            hi, lo = c >> 4, c & 0xF
            ix[2 * i] = (hi - 16 if hi >= 8 else hi) * scale
            ix[2 * i + 1] = (lo - 16 if lo >= 8 else lo) * scale
        new = [0] * 16
        for j in range(2):
            prev = state[16 - order:16] if j == 0 else new[8 - order:8]
            vec = list(prev)
            for i in range(8):
                ind = j * 8 + i
                row = coefs[i]
                acc = 0
                for t in range(order + i):
                    acc += row[t] * vec[t]
                new[ind] = _sat((acc >> 11) + ix[ind])   # >> on Python ints is floor division
                vec.append(ix[ind])
        state = new
        out.extend(new)
    if nsamples is not None:
        out = out[:nsamples]
    return out
