# Testing

| layer | what | when |
|---|---|---|
| tree checks (`./run.sh test --ci`, no ROM) | `rom_leak_check` (no ROM bytes/known SHA in repo), `shz_names_check` (no non-existent sh4zam names; validates against `external/sh4zam/doc/site/dist/api/index.json` or a grep of headers), `diverges_check` (every `DIVERGES` has a reason), `patches_check` (each patch has a `.md`), `vramfence_check`, `no_double_check` (no `double` in sh4zam TUs) | pre-commit |
| purity (`fpu_check.py`) | disassemble objects; forbid FSRRA/FSCA/fmac in physics-pinned TUs; forbid KOS `mat_*` symbols anywhere | pre-commit |
| host tests (`src/dc/test`) | decomp parsers on the fs slice vs expected struct values (swap correctness), MIO0 vs reference, Fast3D interpreter on captured DLs → golden vertex output, mesg-queue semantics, VMU save round trip; sh4zam `SHZ_BACKEND=SW` (algebra only, not accuracy) | `./run.sh test` (needs ROM) |
| replay oracle | `DB_FDR_HASH`: hash of flight-recorder state per frame while playing the ROM's demo replays; must match between builds and between host and target for physics-adjacent changes | per milestone |
| emulator smoke | Flycast boots the `--emu` cdi, serial log shows the milestone marker string | per build |
| hardware | burned CD-R on the retail console: boot, milestone exit criteria, `DB_PERF` numbers over serial if dcload, else on-screen overlay; soak (`DB_SOAK`) 30 min for audio dropouts | per milestone |
| hardware evidence format | `kb/state-log.md` entry: date, disc build id, console, cable (VGA/composite), what was observed, numbers | always |
