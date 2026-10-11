# Disc pipeline

```
baserom.z64 (gitignored, SHA-1 checked)
  ├─ tools/rom/slice_fs.py        → disc/wings64.fs          ROM[0xDE720 .. 0x6314D0)  (filetable+filesys+audio_seq+audio_ctl)
  └─ tools/audio/build_pool.py    → disc/adpcm_pool.bin, disc/sample_table.bin   (from audio_ctl + audio_tbl)
build/wings64.elf  (kos-cc)
disc/disc.id       (build stamp; runtime probes it to choose /cd)
disc/wings64.vmi/.vms icon (optional), disc/README.txt
  └─ mkdcdisc -e build/wings64.elf -d disc/ -n "Pilotwings 64" -a "wings64-dc" -r <YYYYMMDD> -v 1 -o build/wings64.cdi
     variants:  --emu  → adds -N (no padding; emulator/ODE)
                --data-data → -L data-data (late VA2 consoles)
     fallback:  scripts/make_cdi.sh --legacy → objcopy -R .stack -O binary → scramble → makeip → mkisofs -C 0,11702 -G IP.BIN → cdi4dc
checks after build: disc/ contains no file outside the manifest; ip_check (title/product fields); size < 700 MB;
                    sha256 of wings64.fs == expected slice of the maintainer's ROM (recorded locally, never committed)
```

Rules: nothing under `disc/` or `build/` is tracked. `RELEASE=1` builds `build/wings64-release.cdi`
with `db.o` removed and `-DNDEBUG`. Padding stays ON for CD-R images (outer-edge read speed); only
emulator images use `-N`. Boot order on hardware: `/pc` (dcload) → `/cd/disc.id` → `/rd`.
