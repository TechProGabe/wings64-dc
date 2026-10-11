#!/usr/bin/env python3
"""slice_fs.py — cut disc/wings64.fs out of the maintainer's own Pilotwings 64 (US) ROM.

The slice is ROM bytes [0xDE720, 0x6314D0): filetable (UVRM/TABL index) + filesys +
audio_seq + audio_ctl, byte-identical to the ROM (assets stay big-endian; PLAN D2/D3).
audio_tbl is NOT included; it is transcoded to an AICA ADPCM pool by tools/audio/.

The tool refuses any ROM whose SHA-1 is not the pinned US release, then validates the
slice exactly the way the game does (texture.c uvMemInitBlockHdr): decompress the TABL
index, walk every entry from the filesys base and check each one starts with FORM/<tag>.
It also writes disc/fsmeta.txt, which the Dreamcast build reads at boot to prove the
image it loaded from disc is the one that was cut (size, CRC-32, file counts).

Usage: tools/rom/slice_fs.py [--rom baserom.z64] [--out disc] [--quiet]
Nothing it writes may ever be committed (disc/ is gitignored; rom_leak_check enforces).
"""
import argparse
import hashlib
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

ROM_SHA1 = "ec771aedf54ee1b214c25404fb4ec51cfd43191a"   # docs/rom-provenance.md (US NPWE)
ROM_SIZE = 8 * 1024 * 1024
FS_START = 0x0DE720       # filetable (decomp.yaml)
FILESYS = 0x0DF5B0        # filesys
FILESYS_END = 0x618B6C    # end of filesys data (docs/pilotwings64_filesystem.md)
FS_END = 0x6314D0         # audio_tbl start: exclusive end of the slice
ROOT = Path(__file__).resolve().parents[2]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def tag_str(t):
    return t.to_bytes(4, "big").decode("latin-1")


def mio0_decompress(buf, off=0):
    """Standard Nintendo MIO0: 'MIO0', u32 out size, u32 comp offset, u32 uncomp offset, bitstream at +0x10."""
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
    """Return list of (tag, size) from the UVRM/TABL index at fs offset 0, mirroring uvFileReadBlock."""
    if fs[0:4] != b"FORM":
        raise ValueError("filetable does not start with FORM")
    form_len = be32(fs, 4) + 8
    if fs[8:12] != b"UVRM":
        raise ValueError(f"filetable tag is {fs[8:12]!r}, want b'UVRM'")
    entries = []
    off = 12
    while off < form_len:
        tag, size = be32(fs, off), be32(fs, off + 4)
        data = off + 8
        if tag == 0x475A4950:                    # 'GZIP': inner tag, raw size, MIO0 stream
            inner, raw_size = be32(fs, data), be32(fs, data + 4)
            payload = mio0_decompress(fs, data + 8)
            if len(payload) != raw_size:
                raise ValueError("GZIP raw size mismatch")
            tag = inner
        else:
            payload = fs[data:data + size]
        if tag == 0x5441424C:                    # 'TABL'
            for i in range(0, len(payload) - len(payload) % 8, 8):
                entries.append((be32(payload, i), be32(payload, i + 4)))
        off = data + size
    return entries


def walk_filesys(fs, entries):
    """Check every non-empty index entry is FORM/<len>/<tag> at its computed offset. Returns problems."""
    problems = []
    off = FILESYS - FS_START
    for idx, (tag, size) in enumerate(entries):
        if tag != 0:
            if fs[off:off + 4] != b"FORM":
                problems.append(f"entry {idx} {tag_str(tag)} @fs+0x{off:x}: no FORM")
            elif be32(fs, off + 8) != tag:
                problems.append(f"entry {idx} @fs+0x{off:x}: tag {tag_str(be32(fs, off + 8))} != index {tag_str(tag)}")
            elif be32(fs, off + 4) + 8 > size:
                problems.append(f"entry {idx} {tag_str(tag)}: FORM length exceeds index size")
        off += size
    end = off + FS_START
    if not (FILESYS_END <= end <= FILESYS_END + 4):
        problems.append(f"filesys walk ends at ROM 0x{end:x}, want 0x{FILESYS_END:x}")
    return problems, end


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rom", default=str(ROOT / "baserom.z64"))
    ap.add_argument("--out", default=str(ROOT / "disc"))
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    rom_path = Path(a.rom)
    if not rom_path.is_file():
        sys.exit(f"slice_fs: {rom_path.name} not found at the repo root (see docs/rom-provenance.md)")
    rom = rom_path.read_bytes()
    sha = hashlib.sha1(rom).hexdigest()
    if sha != ROM_SHA1 or len(rom) != ROM_SIZE:
        sys.exit(f"slice_fs: refusing ROM with SHA-1 {sha} (size {len(rom)}); only the US release "
                 f"{ROM_SHA1} in big-endian .z64 order is supported")

    fs = rom[FS_START:FS_END]
    entries = parse_index(fs)
    problems, _ = walk_filesys(fs, entries)
    if problems:
        for p in problems[:20]:
            print("slice_fs:", p, file=sys.stderr)
        sys.exit(f"slice_fs: {len(problems)} filesystem problems; slice not written")

    counts = Counter(tag_str(t) for t, _ in entries if t != 0)
    nfiles = sum(counts.values())
    crc = zlib.crc32(fs) & 0xFFFFFFFF

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    fs_path = out / "wings64.fs"
    if not fs_path.exists() or fs_path.read_bytes() != fs:
        fs_path.write_bytes(fs)
    meta = [
        "# wings64.fs manifest written by tools/rom/slice_fs.py; read at boot by src/dc/rom/romfs.c",
        f"rom_start=0x{FS_START:X}",
        f"size={len(fs)}",
        f"crc32=0x{crc:08X}",
        f"index_entries={len(entries)}",
        f"uv_files={nfiles}",
    ] + [f"count.{k}={counts[k]}" for k in sorted(counts)]
    (out / "fsmeta.txt").write_text("\n".join(meta) + "\n")

    if not a.quiet:
        print(f"slice_fs: ROM SHA-1 ok; wings64.fs = ROM[0x{FS_START:X}..0x{FS_END:X}) "
              f"{len(fs)} bytes ({len(fs) / 1048576:.2f} MiB) crc32 0x{crc:08X}")
        print(f"slice_fs: {len(entries)} index entries, {nfiles} UV files, all FORM headers verified")
        print("slice_fs: " + " ".join(f"{k}={counts[k]}" for k in sorted(counts)))


if __name__ == "__main__":
    main()
