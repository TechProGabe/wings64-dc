#!/usr/bin/env python3
"""measure_pool.py -- measure the AICA sound-RAM pool needed by Pilotwings 64 audio.

Reads baserom.z64 (US release, SHA-1 pinned), walks the music ALBankFile (audio_ctl) and
the SFX UVSX bank (.CTL + .TBL chunks inside the filesys), de-duplicates waves by
(base, len), and reports their size once transcoded to Yamaha 4-bit ADPCM
(ceil(samples / 2) bytes). Compares the resident pool against 2 MiB minus 256 KiB.

Prints a report to stdout only; writes nothing and copies no ROM bytes.
Usage: python3 -I tools/audio/measure_pool.py   (run from the repo root)
"""
import hashlib
import struct
import sys
import zlib  # noqa: F401  (kept for parity with slice_fs.py helpers)
from pathlib import Path

ROM_SHA1 = "ec771aedf54ee1b214c25404fb4ec51cfd43191a"
ROM_SIZE = 8 * 1024 * 1024
FS_START = 0x0DE720
FILESYS = 0x0DF5B0
FILESYS_END = 0x618B6C
FS_END = 0x6314D0            # audio_tbl start (music wave table)
CTL_START = 0x62D460         # audio_ctl (music ALBankFile)
AL_BANK_VERSION = 0x4231
ADPCM_STATE_BYTES = 16 * 2   # ADPCMFSIZE shorts (abi.h)
AICA_RAM = 2 * 1024 * 1024
RESERVED = 256 * 1024
POOL_LIMIT = AICA_RAM - RESERVED
CHANNEL_MAX_SAMPLES = 65534
ROOT = Path(__file__).resolve().parents[2]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def be16s(b, o):
    return struct.unpack_from(">h", b, o)[0]


def tag_str(t):
    return t.to_bytes(4, "big").decode("latin-1")


def mio0_decompress(buf, off=0):
    """Standard Nintendo MIO0 (same algorithm as tools/rom/slice_fs.py)."""
    if buf[off:off + 4] != b"MIO0":
        raise ValueError(f"MIO0 magic missing at 0x{off:x}")
    out_size, comp_off, raw_off = struct.unpack_from(">III", buf, off + 4)
    out = bytearray()
    bits_off, comp, raw = off + 0x10, off + comp_off, off + raw_off
    word, nbits = 0, 0
    while len(out) < out_size:
        if nbits == 0:
            word = be32(buf, bits_off)
            bits_off += 4
            nbits = 32
        bit = word & 0x80000000
        word = (word << 1) & 0xFFFFFFFF
        nbits -= 1
        if bit:
            out.append(buf[raw])
            raw += 1
        else:
            v = (buf[comp] << 8) | buf[comp + 1]
            comp += 2
            length = (v >> 12) + 3
            dist = (v & 0xFFF) + 1
            for _ in range(length):
                out.append(out[-dist])
    return bytes(out[:out_size])


def parse_index(fs):
    """UVRM/TABL index at fs offset 0 (mirrors slice_fs.parse_index)."""
    if fs[0:4] != b"FORM" or fs[8:12] != b"UVRM":
        raise ValueError("filetable is not FORM/UVRM")
    form_len = be32(fs, 4) + 8
    entries = []
    off = 12
    while off < form_len:
        tag, size = be32(fs, off), be32(fs, off + 4)
        data = off + 8
        if tag == 0x475A4950:  # GZIP
            inner, raw_size = be32(fs, data), be32(fs, data + 4)
            payload = mio0_decompress(fs, data + 8)
            if len(payload) != raw_size:
                raise ValueError("GZIP raw size mismatch in index")
            tag = inner
        else:
            payload = fs[data:data + size]
        if tag == 0x5441424C:  # TABL
            for i in range(0, len(payload) - len(payload) % 8, 8):
                entries.append((be32(payload, i), be32(payload, i + 4)))
        off = data + size
    return entries


def parse_chunks(form, label):
    """Yield (tag, payload) for each sub-chunk of a FORM body (skips PAD ). GZIP is unwrapped."""
    chunks = []
    off = 12  # past FORM, size, form type
    end = min(len(form), be32(form, 4) + 8)
    while off + 8 <= end:
        tag, size = be32(form, off), be32(form, off + 4)
        data = off + 8
        if tag == 0x475A4950:  # GZIP
            inner, raw_size = be32(form, data), be32(form, data + 4)
            payload = mio0_decompress(form, data + 8)
            if len(payload) != raw_size:
                raise ValueError(f"{label}: GZIP raw size mismatch")
            chunks.append((inner, payload, size, True))
        else:
            chunks.append((tag, form[data:data + size], size, False))
        off = data + size
    return chunks


def wave_info(bank, base_off, tbl_len, tbl_name, wave_off):
    """Read one ALWaveTable at bank offset wave_off."""
    base = be32(bank, wave_off)
    length = be32(bank, wave_off + 4)
    wtype = bank[wave_off + 8]
    loop_ptr = be32(bank, wave_off + 12)
    loop = None
    if loop_ptr:
        o = loop_ptr
        if wtype == 0:   # AL_ADPCM_WAVE: ALADPCMloop start,end,count,state
            loop = (be32(bank, o), be32(bank, o + 4), be32(bank, o + 8))
        else:            # AL_RAW16_WAVE: ALRawLoop start,end,count
            loop = (be32(bank, o), be32(bank, o + 4), be32(bank, o + 8))
    return {"base": base, "len": length, "type": wtype, "loop": loop}


def walk_bank_file(ctl, tbl_len, label):
    """Walk ALBankFile -> banks -> instruments (+percussion) -> sounds -> wavetables.
    Returns list of unique wave dicts (deduped by base,len) plus usage counts."""
    rev = struct.unpack_from(">h", ctl, 0)[0]
    if rev != AL_BANK_VERSION:
        raise ValueError(f"{label}: ALBankFile revision 0x{rev & 0xFFFF:04x}, want 0x4231 ('B1')")
    bank_count = struct.unpack_from(">h", ctl, 2)[0]
    print(f"[{label}] ALBankFile revision 0x{rev & 0xFFFF:04X} banks={bank_count} ctl={len(ctl)} bytes")

    waves = {}          # (base,len) -> info
    refs = 0
    insts = 0
    sounds_seen = 0
    for b in range(bank_count):
        bank_off = be32(ctl, 4 + 4 * b)
        inst_count = struct.unpack_from(">h", ctl, bank_off)[0]
        perc = be32(ctl, bank_off + 8)
        inst_ptrs = [be32(ctl, bank_off + 12 + 4 * i) for i in range(inst_count)]
        inst_list = list(inst_ptrs)
        if perc:
            inst_list.insert(0, perc)
        for io in inst_list:
            insts += 1
            sound_count = struct.unpack_from(">h", ctl, io + 14)[0]
            for s in range(sound_count):
                snd = be32(ctl, io + 16 + 4 * s)
                sounds_seen += 1
                wt = be32(ctl, snd + 8)
                if not wt:
                    continue
                refs += 1
                w = wave_info(ctl, 0, tbl_len, label, wt)
                key = (w["base"], w["len"])
                if key not in waves:
                    waves[key] = w
                    w["bank_type"] = w["type"]
                    w["ref_count"] = 1
                else:
                    waves[key]["ref_count"] += 1
    print(f"[{label}] banks={bank_count} instruments_walked={insts} sounds={sounds_seen} "
          f"wave_refs={refs} unique_waves={len(waves)}")
    return list(waves.values())


def measure(waves, tbl_len, label):
    rows = []
    total = {"vadpcm": 0, "samples": 0, "yamaha": 0, "pcm16": 0}
    over = []
    raw_count = 0
    for w in waves:
        if w["type"] == 0:
            samples = w["len"] // 9 * 16
            kind = "ADPCM"
        else:
            samples = w["len"] // 2
            kind = "RAW16"
            raw_count += 1
        yamaha = (samples + 1) // 2
        pcm = samples * 2
        if w["base"] + w["len"] > tbl_len:
            print(f"[{label}] WARNING wave base=0x{w['base']:x} len={w['len']} runs past audio_tbl end")
        total["vadpcm"] += w["len"]
        total["samples"] += samples
        total["yamaha"] += yamaha
        total["pcm16"] += pcm
        if samples > CHANNEL_MAX_SAMPLES:
            over.append((w["base"], samples))
        rows.append({"label": label, "base": w["base"], "len": w["len"], "kind": kind,
                     "samples": samples, "yamaha": yamaha, "loop": w["loop"],
                     "refs": w["ref_count"]})
    return rows, total, over, raw_count


def fmt(n):
    return f"{n:,} ({n / 1048576:.3f} MiB)"


def main():
    rom_path = ROOT / "baserom.z64"
    if not rom_path.is_file():
        sys.exit("measure_pool: baserom.z64 not found at the repo root")
    rom = rom_path.read_bytes()
    sha = hashlib.sha1(rom).hexdigest()
    if sha != ROM_SHA1 or len(rom) != ROM_SIZE:
        sys.exit(f"measure_pool: refusing ROM with SHA-1 {sha} (size {len(rom)}); "
                 f"want US release {ROM_SHA1}")
    print(f"measure_pool: ROM SHA-1 ok")

    # ---- music bank ----
    music_ctl = rom[CTL_START:FS_END]
    tbl = rom[FS_END:]
    tbl_end_max = 0
    # tbl length is computed from waves below; read whole remainder as a view bound only
    music_waves = walk_bank_file(music_ctl, None, "music")
    for w in music_waves:
        tbl_end_max = max(tbl_end_max, w["base"] + w["len"])
    print(f"[music] audio_tbl max(base+len) = {tbl_end_max} bytes -> audio_tbl end at "
          f"ROM 0x{FS_END + tbl_end_max:X} (raw size {tbl_end_max} = {tbl_end_max / 1048576:.3f} MiB)")
    music_rows, music_tot, music_over, music_raw = measure(music_waves, tbl_end_max, "music")

    # ---- SFX bank: UVSX in filesys ----
    fs = rom[FS_START:FS_END]
    entries = parse_index(fs)
    off = FILESYS - FS_START
    sfx_rows, sfx_tot, sfx_over, sfx_raw = [], {"vadpcm": 0, "samples": 0, "yamaha": 0, "pcm16": 0}, [], 0
    sfx_found = False
    for idx, (tag, size) in enumerate(entries):
        if tag == 0:
            off += size
            continue
        if tag_str(tag) == "UVSX":
            sfx_found = True
            form = fs[off:off + min(size, be32(fs, off + 4) + 8)]
            print(f"[sfx] UVSX entry {idx} at ROM 0x{FS_START + off:X}, FORM length {len(form)}")
            chunks = parse_chunks(form, "sfx")
            print("[sfx] UVSX chunks (tag, stored size, decoded bytes, gzip):")
            ctl = tbl = None
            for ctag, payload, csize, gz in chunks:
                name = tag_str(ctag)
                print(f"    {name!r:8} stored={csize:>9} decoded={len(payload):>9} gzip={gz}")
                norm = name.strip(". ").upper()
                if norm == "CTL" and ctl is None:
                    ctl = payload
                elif norm == "TBL" and tbl is None:
                    tbl = payload
            if ctl is None or tbl is None:
                print("[sfx] UVSX does not contain both a .CTL and a .TBL chunk; SFX NOT measured")
            else:
                print(f"[sfx] .CTL {len(ctl)} bytes, .TBL {len(tbl)} bytes")
                sfx_waves = walk_bank_file(ctl, None, "sfx")
                sfx_end = max((w["base"] + w["len"]) for w in sfx_waves) if sfx_waves else 0
                print(f"[sfx] .TBL max(base+len) = {sfx_end} bytes; .TBL chunk is {len(tbl)} bytes"
                      + ("" if sfx_end <= len(tbl) else "  ** WAVES RUN PAST TBL **"))
                sfx_rows, sfx_tot, sfx_over, sfx_raw = measure(sfx_waves, sfx_end, "sfx")
            break
        off += size
    if not sfx_found:
        print("[sfx] no UVSX entry found in the filesys; SFX NOT measured")

    # ---- report ----
    print()
    print("=" * 72)
    print("1. Per-group totals (unique waves, deduped by (base,len) within each bank)")
    print("=" * 72)
    for name, rows, tot, over, raw in (("MUSIC", music_rows, music_tot, music_over, music_raw),
                                       ("SFX", sfx_rows, sfx_tot, sfx_over, sfx_raw)):
        print(f"{name}: unique waves = {len(rows)} (RAW16 = {raw})")
        print(f"  VADPCM bytes     : {fmt(tot['vadpcm'])}")
        print(f"  total samples    : {tot['samples']:,}")
        print(f"  Yamaha ADPCM     : {fmt(tot['yamaha'])}")
        print(f"  PCM16 bytes      : {fmt(tot['pcm16'])}")
        print(f"  waves > {CHANNEL_MAX_SAMPLES} samples: {len(over)}")
        for base, samples in over:
            print(f"      base=0x{base:X} samples={samples:,}")
    print()
    print("Expected ~57 music + ~107 SFX waves; got "
          f"{len(music_rows)} music, {len(sfx_rows)} SFX"
          + ("" if (len(music_rows) == 57 and len(sfx_rows) == 107) else "  ** COUNT DIFFERS **"))

    grand = music_tot["yamaha"] + sfx_tot["yamaha"]
    print()
    print("=" * 72)
    print("2. Pool budget")
    print("=" * 72)
    print(f"Grand total Yamaha ADPCM : {fmt(grand)}")
    print(f"Fraction of 2 MiB ({AICA_RAM}) : {grand / AICA_RAM * 100:.2f}%")
    print(f"Pool limit (2 MiB - 256 KiB) : {POOL_LIMIT:,} ({POOL_LIMIT / 1048576:.3f} MiB)")
    print(f"Headroom after 256 KiB reserve: {POOL_LIMIT - grand:,} bytes "
          f"({(POOL_LIMIT - grand) / 1048576:.3f} MiB)")
    print()
    print("=" * 72)
    print("3. 10 largest waves by Yamaha ADPCM bytes")
    print("=" * 72)
    print(f"{'bank':6} {'base':>10} {'samples':>10} {'adpcm_bytes':>12} {'looped':>7} {'type':>6}")
    rows = sorted(music_rows + sfx_rows, key=lambda r: r["yamaha"], reverse=True)[:10]
    for r in rows:
        print(f"{r['label']:6} 0x{r['base']:08X} {r['samples']:>10,} {r['yamaha']:>12,} "
              f"{'yes' if r['loop'] else 'no':>7} {r['kind']:>6}")
    print()
    verdict = "FITS" if grand <= POOL_LIMIT else "DOES NOT FIT"
    print(f"VERDICT: {verdict} (resident pool {grand:,} bytes vs limit {POOL_LIMIT:,} bytes)")


if __name__ == "__main__":
    main()
