# STATE — current numbers and the ranked queue

Updated 2026-10-10 (M0 done: emulator gate PASS).

| metric | value | evidence |
|---|---|---|
| milestone | **M0 done** (emulator PASS); M1 next | state-log 2026-10-10 M0 gate |
| M0 disc | `build/wings64.cdi` build `574fd2b-1011-0141` (705 MB raw, padded, 80-min CD-R) sha256 `fd7fed8e46ec7887…` | `./run.sh disc` |
| toolchain | image `wings64-dc:ce773e5503b9`: GCC 14.3.0, KOS v2.2.2 `c23410b` (-m4-single-only), mkdcdisc v0.0.4 `2b98b0d` | `docker/pins.env` |
| decomp pin | `1a8558f`, 0 patches | `scripts/fetch_decomp.sh` |
| sh4zam pin | fork `7747e5d` (v0.9.1), built from source in `src/game/Makefile` | submodule |
| ROM fs slice | **5,582,256 B (5.32 MiB)**, crc32 `DAFCC147`, 1272 index entries = 1272 UV files, every FORM header verified | `tools/rom/slice_fs.py` |
| UV file mix | UVTX 463, UVMD 363, UVAN 115, UVBT 102, UVCT 101, UPWT 61, PDAT 25, 3VUE 12, UVFT 9, SPTH 8, UPWL 4, 1 each UVEN/UVLT/UVLV/UVSQ/UVSX/UVSY/UVTP/UVTR/ADAT | slice_fs |
| ELF size | text 300,260 · data 13,596 · bss 46,516 (KOS + port + sh4zam) | `kos-size` |
| emulator gate | **PASS** `WINGS64_M0_OK`, screen all green (Flycast v2.7 container, HLE BIOS, composite) | build/emu-shot-2.png |
| boot load (emulator) | 5.32 MiB in 2881 ms = 1892 KB/s (not hardware; CD-R expected 500 KB/s–1 MB/s) | serial log |
| fps on hardware | — (target: locked 30) | — |
| audio_tbl raw | ≈1.9 MB VADPCM — ADPCM pool size **unknown, measure in M1** | — |

## M0 exit criteria (met in emulator 2026-10-10)
Screen shows, all green: `crc32 DAFCC147 manifest DAFCC147 match`, `UV files 1272 (index 1272, manifest 1272)`,
`FORM headers ok 1272/1272 walk end 618B6C`, `M0 RESULT: PASS`, and the `alive N s` counter keeps ticking.

## Ranked queue
1. M1 compile decomp for SH-4 + host; AICA pool size tool (de-risk H3 early).
2. M2 boot to title.
