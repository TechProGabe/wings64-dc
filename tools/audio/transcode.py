#!/usr/bin/env python3
"""transcode.py -- build the AICA sample pool disc file (docs/audio-design.md section 4).

Reads baserom.z64 (SHA-1 gated), walks the music ALBankFile (audio_ctl + audio_tbl) and the SFX UVSX
bank (.CTL + .TBL inside the filesys), decodes every unique wave (VADPCM, tools/audio/vadpcm.py),
truncates looped waves at their loop end, decimates waves still over 65534 samples 2x (11025 Hz),
and encodes Yamaha ADPCM (or PCM8/PCM16 for waves listed in tools/audio/pcm_force.txt).

Writes build/audio/wings64.aud (ROM-derived: never commit it; rom_leak_check rejects *.aud) and prints
a report. Exit 1 when SFX + music do not both fit AICA sample RAM: the driver uploads the whole pool at
boot (amended ruling 1 in docs/audio-design.md; no music paging).

File layout (little-endian):
  header  "W64A" u32 version=1 u32 n_entries u32 sfx_off u32 sfx_bytes u32 mus_off u32 mus_bytes
  entries n x 32 B, sorted by (bank, base, vlen):
          u8 bank (0 sfx, 1 music) u8 fmt (0 ADPCM, 1 PCM8, 2 PCM16) u16 rate_hz
          u32 base (offset in its .TBL) u32 vlen (VADPCM bytes) u32 off (in its blob, 32-aligned)
          u32 bytes u32 samples (loop end / play length, <= 65534) u32 loopstart u32 loopend (~0 = none)
  blobs   SFX blob at sfx_off, music blob at mus_off (both 32-aligned)
Usage: python3 -I tools/audio/transcode.py [--out PATH] [--no-decimate] [--jobs N]
"""
import argparse
import math
import os
import struct
import sys
from multiprocessing import Pool
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import measure_pool as mp  # noqa: E402
import vadpcm  # noqa: E402
import yamaha  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
MAGIC, VERSION = b"W64A", 1
FMT_ADPCM, FMT_PCM8, FMT_PCM16 = 0, 1, 2
FMT_NAMES = {"adpcm": FMT_ADPCM, "pcm8": FMT_PCM8, "pcm16": FMT_PCM16}
RATE = 22050
MAX_SAMPLES = 65534
PAD = 32                       # extra samples after the play end (loop continuation or silence)
AICA_SAMPLE_RAM = 0x200000 - 0x030000   # KOS v2.2.2 AICA_RAM_START..AICA_RAM_END
SLACK = 8 * 1024              # snd_mem_malloc 32-byte rounding + headroom
NO_LOOP = 0xFFFFFFFF


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def collect_waves(ctl, tbl, bank_id):
    """Unique ALWaveTables of an ALBankFile, keyed (base, len), with book and loop."""
    waves = {}
    bank_count = struct.unpack_from(">h", ctl, 2)[0]
    for b in range(bank_count):
        bo = be32(ctl, 4 + 4 * b)
        inst_count = struct.unpack_from(">h", ctl, bo)[0]
        perc = be32(ctl, bo + 8)
        insts = [be32(ctl, bo + 12 + 4 * i) for i in range(inst_count)] + ([perc] if perc else [])
        for io in insts:
            for s in range(struct.unpack_from(">h", ctl, io + 14)[0]):
                wt = be32(ctl, be32(ctl, io + 16 + 4 * s) + 8)
                if not wt:
                    continue
                base, length, wtype = be32(ctl, wt), be32(ctl, wt + 4), ctl[wt + 8]
                if (base, length) in waves:
                    continue
                if wtype != 0:
                    sys.exit(f"transcode: bank {bank_id} wave 0x{base:x} is not ADPCM (type {wtype})")
                lp, bk = be32(ctl, wt + 12), be32(ctl, wt + 16)
                order, npred = struct.unpack_from(">ii", ctl, bk)
                book = list(struct.unpack_from(f">{order * npred * 8}h", ctl, bk + 8))
                loop = state = None
                if lp:
                    ls, le, cnt = struct.unpack_from(">IIi", ctl, lp)
                    if cnt != -1:
                        sys.exit(f"transcode: wave 0x{base:x} loop count {cnt} (only infinite loops supported)")
                    loop = (ls, le)
                    state = list(struct.unpack_from(">16h", ctl, lp + 12))
                waves[(base, length)] = {"bank": bank_id, "base": base, "vlen": length, "order": order,
                                         "npred": npred, "book": book, "loop": loop, "loop_state": state,
                                         "data": bytes(tbl[base:base + length])}
    return list(waves.values())


def load_rom_waves():
    rom_path = ROOT / "baserom.z64"
    if not rom_path.is_file():
        sys.exit("transcode: baserom.z64 not found at the repo root")
    rom = rom_path.read_bytes()
    import hashlib
    if hashlib.sha1(rom).hexdigest() != mp.ROM_SHA1:
        sys.exit("transcode: ROM SHA-1 mismatch (want the US release, docs/rom-provenance.md)")
    music = collect_waves(rom[mp.CTL_START:mp.FS_END], rom[mp.FS_END:], 1)
    fs = rom[mp.FS_START:mp.FS_END]
    off = mp.FILESYS - mp.FS_START
    sfx = None
    for tag, size in mp.parse_index(fs):
        if tag and mp.tag_str(tag) == "UVSX":
            form = fs[off:off + min(size, be32(fs, off + 4) + 8)]
            ctl = tbl = None
            for ctag, payload, _, _ in mp.parse_chunks(form, "sfx"):
                name = mp.tag_str(ctag).strip(". ").upper()
                if name == "CTL" and ctl is None:
                    ctl = payload
                elif name == "TBL" and tbl is None:
                    tbl = payload
            sfx = collect_waves(ctl, tbl, 0)
            break
        off += size
    if sfx is None:
        sys.exit("transcode: no UVSX bank in the filesys")
    return sfx, music


def read_force():
    force = {}
    p = ROOT / "tools/audio/pcm_force.txt"
    if p.is_file():
        for ln in p.read_text().splitlines():
            ln = ln.split("#", 1)[0].split()
            if not ln:
                continue
            bank, base = ln[0].split(":")
            fmt = FMT_NAMES[ln[1].lower()] if len(ln) > 1 else FMT_PCM8
            force[({"sfx": 0, "mus": 1}[bank], int(base, 0))] = fmt
    return force


def lowpass_taps(n=31, cutoff=0.225):
    """Windowed-sinc lowpass, cutoff in cycles/sample of the input rate (0.225 -> 4.96 kHz at 22050)."""
    h, m = [], (n - 1) / 2
    for i in range(n):
        x = i - m
        s = 2 * cutoff if x == 0 else math.sin(2 * math.pi * cutoff * x) / (math.pi * x)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) + 0.08 * math.cos(4 * math.pi * i / (n - 1))
        h.append(s * w)
    g = sum(h)
    return [v / g for v in h]


def extend(pcm, end, loop, count):
    """pcm[0:end] followed by `count` continuation samples (loop wrap or silence)."""
    out = list(pcm[:end])
    if loop:
        ls = loop[0]
        period = end - ls
        out += [pcm[ls + (i % period)] for i in range(count)]
    else:
        out += [0] * count
    return out


def decimate(pcm, end, loop):
    taps = lowpass_taps()
    half = len(taps) // 2
    src = extend(pcm, end, loop, PAD * 2 + half + 2)
    src = [src[0]] * half + src     # hold the first sample for the filter's left edge
    out = []
    for k in range(0, end + PAD * 2, 2):
        acc = 0.0
        for t, c in enumerate(taps):
            acc += c * src[k + t]
        v = int(round(acc))
        out.append(32767 if v > 32767 else (-32768 if v < -32768 else v))
    return out


def transcode_one(args):
    w, fmt, allow_decimate = args
    n = len(w["data"]) // 9 * 16
    pcm = vadpcm.decode(w["data"], w["order"], w["npred"], w["book"], n)
    loop = w["loop"]
    end = min(loop[1], n) if loop else n
    rate = RATE
    if end > MAX_SAMPLES:
        if not allow_decimate:
            end = MAX_SAMPLES
            loop = (min(loop[0], end - 16), end) if loop else None
        else:
            body = decimate(pcm, end, loop)
            rate = RATE // 2
            loop = (loop[0] // 2, end // 2) if loop else None
            end = end // 2
            pcm = body
    if rate == RATE:
        body = extend(pcm, end, loop, PAD)
    if end > MAX_SAMPLES:
        raise SystemExit(f"transcode: wave 0x{w['base']:x} still {end} samples after decimation")
    if fmt == FMT_ADPCM:
        data = yamaha.encode(body)
    elif fmt == FMT_PCM8:
        data = bytes(((s >> 8) & 0xFF) for s in body)
    else:
        data = struct.pack(f"<{len(body)}h", *body)
    return {"bank": w["bank"], "base": w["base"], "vlen": w["vlen"], "fmt": fmt, "rate": rate,
            "samples": end, "loop": loop, "data": data, "decimated": rate != RATE}


def align32(n):
    return (n + 31) & ~31


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/audio/wings64.aud"))
    ap.add_argument("--no-decimate", action="store_true", help="truncate over-length waves instead")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()

    sfx, music = load_rom_waves()
    force = read_force()
    jobs = [(w, force.get((w["bank"], w["base"]), FMT_ADPCM), not a.no_decimate) for w in sfx + music]
    with Pool(a.jobs) as pool:
        res = pool.map(transcode_one, jobs, chunksize=1)
    res.sort(key=lambda r: (r["bank"], r["base"], r["vlen"]))

    blobs = {0: bytearray(), 1: bytearray()}
    entries = []
    for r in res:
        blob = blobs[r["bank"]]
        off = len(blob)
        blob += r["data"]
        blob += bytes(align32(len(blob)) - len(blob))
        ls, le = r["loop"] if r["loop"] else (NO_LOOP, NO_LOOP)
        entries.append(struct.pack("<BBHIIIIIII", r["bank"], r["fmt"], r["rate"], r["base"], r["vlen"], off,
                                   len(r["data"]), r["samples"], ls, le))
    hdr_size = 28 + 32 * len(entries)
    sfx_off = align32(hdr_size)
    mus_off = align32(sfx_off + len(blobs[0]))
    out = bytearray(struct.pack("<4sIIIIII", MAGIC, VERSION, len(entries), sfx_off, len(blobs[0]), mus_off,
                                len(blobs[1])))
    out += b"".join(entries)
    out += bytes(sfx_off - len(out))
    out += blobs[0]
    out += bytes(mus_off - len(out))
    out += blobs[1]
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_bytes(out)

    sfx_b, mus_b = len(blobs[0]), len(blobs[1])
    room = AICA_SAMPLE_RAM - SLACK - sfx_b - mus_b
    print(f"transcode: {len(res)} waves (sfx {len(sfx)}, music {len(music)}) -> {a.out} ({len(out):,} B)")
    for r in res:
        if r["decimated"] or r["fmt"] != FMT_ADPCM:
            print(f"  {'sfx' if r['bank'] == 0 else 'mus'}:0x{r['base']:x} fmt={r['fmt']} rate={r['rate']} "
                  f"samples={r['samples']}{' (decimated 2x)' if r['decimated'] else ''}")
    print(f"  sfx pool {sfx_b:,} B + music pool {mus_b:,} B = {sfx_b + mus_b:,} B vs AICA sample RAM "
          f"{AICA_SAMPLE_RAM:,} B - {SLACK // 1024} KiB slack")
    if room < 0:
        print(f"transcode: BUDGET FAIL: over by {-room:,} B (all waves must be resident)")
        return 1
    print(f"transcode: budget PASS (headroom {room:,} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
