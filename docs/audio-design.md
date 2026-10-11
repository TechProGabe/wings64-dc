# Audio design (M4)

Design consult (Fable, 2026-10-11) written before any audio code; executed in M4. **Read "Amendments
from execution" at the end first**: measurements during M4 changed rulings 1, 3, 6 and 8. Sources read:
`audio_manager.c`, `audio_seq.c`, `audio_emitter.c`, `libultra/audio/{synthesizer,syn*,env,load,resample,sndplayer,csplayer}.c`,
`src/dc/ultra/os_shim.c`, `src/dc/rom/pw64_audio_load.c`, `tools/audio/measure_pool.py`, PLAN D6/M4, `kb/traps.md`,
Birdman64/sm64-dc brief, plus the KOS v2.2.2 AICA facts verified in the container (sample RAM 0x030000..0x200000 =
1,900,544 B; `aica_channel_t` vol 0-255 / pan 0-255; no firmware envelopes; loop regs `& 0xffff`).

## Decision table

| # | Question | Ruling |
|---|---|---|
| 1 | Residency | SFX pool resident in AICA RAM for the whole run (1 UVSX bank, loaded once). Music pool resident in **main RAM** (~0.69 MiB), uploaded per wave into a music region of AICA RAM with refcount + LRU (sm64-dc lineage). No per-level/per-sequence disc paging. |
| 2 | Waves > 65534 samples | **Decimate 2x offline** (`rate_hz=11025`), music and SFX alike; no streaming channel in M4. |
| 3 | >+1 octave pitch | Offline per-wave decision -> **PCM8**; music from keymap analysis, SFX from a checked-in `pcm_force.txt` fed by a runtime clamp counter. Runtime never switches formats; it clamps frequency and counts. |
| 4 | audio_tbl | Offline transcode at disc build (`tools/audio/transcode.py`) -> one disc file `disc/wings64.aud` (ISO `WINGS64.AUD`) = header + sample table + SFX blob + music blob. Never in the RAM image, never on the SH-4 at boot. |
| 5 | Cut point | Replace the **alSyn voice API + ALSynth driver** (synthesizer.c and syn*.c excluded from the build, reimplemented in `src/dc/audio/aica_synth.c`). Players (ALCSPlayer/ALSndPlayer) untouched. PVoice i -> AICA channel i (0-31). |
| 6 | Timing | Keep `__amMain` on the scheduler retrace (it already is a KOS thread). Virtual AI DAC in `os_shim.c` (osAiSetNextBuffer/GetLength) so frameSamples self-regulates to 22050 Hz; deficit capped at 4 frames. |
| 7 | Gate | Serial counters + channel snapshots; `WINGS64_M4_OK` + `WINGS64_M4_SOAK_OK`; host tests for the codec; optional SDL "disk" audio capture in Flycast for an RMS check. |
| 8 | Kill switches | `WINGS64_AUDIO`, `_MUSIC`, `_SFX`, `_MUSIC_LRU`, `_STEAL_SOFT`, `_RAMPS`, `_BATCH`, `WINGS64_AICA_ADPCM_FMAX`, `WINGS64_AUDIO_CATCHUP_MAX`, `DB_AUDIO_LOG`, `DB_AUDIO_SNAP`. |

## 1. Residency
SFX (107 waves, ~1.21 MiB Yamaha, ~1.18 MiB after decimating its 4 long waves) is one UVSX bank, loaded once in
`uvaManager_80204518`, and SFX must start with zero latency during flight, so it is uploaded once at boot and never
moves. Music (~0.71 MiB, ~0.69 after decimation) nearly fits in what is left (1,900,544 - 1,180,000 ~ 700 KiB), so an
LRU music region is a safety valve that rarely evicts, not a hot path; it also absorbs PCM8 growth. Per-sequence disc
paging is rejected: it needs the sequence->wave analysis, costs CD-R seeks on level transitions, and the LRU gives the
same ARAM footprint with less code. Main-RAM cost 0.69 MiB; SFX goes disc -> bounce -> `spu_memload_sq`, no main-RAM copy.

## 2. Long waves
6 waves (2 music, 4 SFX, all looped) exceed 65534 samples, which the KOS firmware also enforces (`& 0xffff`).
Decimation is the smallest hardware-sane design: no dedicated channel, no timer-estimated read head, no SH-4 ADPCM
decode in the frame, and it saves ~140 KiB of pool. Cost: 11 kHz content on 6 loops (engine/wind/pads); loop points
carried /2 (loopstart rounded down to even, loopend up). Streaming (sm64-dc ring) stays a lever for M5 if a wave sounds
unacceptable. Splitting is rejected: gapless chaining needs sub-frame channel polling.

## 3. PCM fallback
Format is fixed offline per wave (runtime switching would need both formats resident). `transcode.py` marks a music
wave PCM8 when any keymap using it implies ratio > FMAX/22050 (`2^((keyMax - keyBase + bendRange)/12)`, bendRange = 2
semitones default); SFX pitch is a runtime f32 (`uvEmitterSetPitch`), so SFX entries come from
`tools/audio/pcm_force.txt` (key `sfx:<base>` or `mus:<base>` plus optional `pcm16`), filled from gate-run logs: when a
voice requests `hz > FMAX` on an ADPCM wave, the driver clamps to FMAX and logs `aud: clamp wave=<id> hz=<n>`
(rate-limited). PCM8 (2x ADPCM bytes) is the default fallback; PCM16 only by explicit key. Budget rule enforced by the
tool: `sfx_bytes + 256 KiB (music region minimum) + 32 KiB <= 1,900,544`. **Verify on hardware**: whether the AICA ADPCM
ceiling is 88200 Hz or 44100 Hz; default `WINGS64_AICA_ADPCM_FMAX=88200`, the hardware test A/Bs 44100. Flycast does
not model the ceiling.

## 4. Where audio_tbl lives, format
Offline: the SH-4 would need a VADPCM decoder plus a Yamaha encoder at boot (seconds of CPU, no host test surface);
offline gives deterministic output, a size report, and a budget check at build time. Disc file (little-endian, new
material written by the host, not a ROM slice, so D3 does not apply):
header `{magic "W64A", u32 version=1, u32 n_entries, u32 sfx_off, u32 sfx_bytes, u32 mus_off, u32 mus_bytes}`;
entry `{u8 bank (0 sfx, 1 mus), u8 fmt (0 ADPCM, 1 PCM8, 2 PCM16), u16 rate_hz (22050|11025), u32 base (offset within
its .TBL), u32 vlen (VADPCM bytes), u32 off (within the blob, 32-aligned), u32 bytes, u32 samples (<= 65534),
u32 loopstart, u32 loopend (0xFFFFFFFF = none)}`. Built into `build/audio/wings64.aud`, copied to `disc/` (gitignored;
`rom_leak_check.py` rejects `*.aud`). Boot cost +1.9 MB from CD-R (~2-4 s, hypothesis).

## 5. Interception point and driver
Players use only `drvr->head` and `drvr->curSamples`; nothing dereferences `ALVoice.pvoice`. So the build excludes
`libultra/audio/{synthesizer,synallocvoice,synfreevoice,synstartvoice,synstartvoiceparam,synstopvoice,synsetpitch,
synsetvol,synsetpan,synsetfxmix,synsetpriority,synallocfx,syndelete,env,load,resample,mainbus,auxbus,reverb,filter,
copy,save}.c` (build-system change, not a decomp edit; keep `drvrnew.c`, `synaddplayer.c`, `bnkf.c`, heaps,
cseq/seq/sndp/csplayer). `src/dc/audio/aica_synth.c` defines the same symbols: `alSynNew`, `alAudioFrame` (the client
loop of synthesizer.c re-expressed: call `handler` while `paramSamples - curSamples < outLen`, then
`curSamples += outLen`, then flush), `alSynAllocVoice` (free list, else steal lowest priority <= requested),
`alSynFreeVoice`, `alSynStartVoice(Params)`, `alSynStopVoice`, `alSynSetVol/Pitch/Pan/FXMix/Priority`,
`alSynAllocFX`=NULL, `alSynDelete`. Consuming the Acmd stream is rejected (an RSP mixing program; SH-4 would mix);
patching PVoice internals is rejected.

**Trap**: `alAudioFrame` must return `*cmdLen = 1` (one dummy Acmd), otherwise `__amHandleFrameMsg` returns 0,
`lastInfo` never updates and `osAiSetNextBuffer` is never called (frameSamples sticks high = fast tempo).

Voice -> channel: `dc_voice_t[32]`, voice i owns AICA channel i (32-63 reserved for a later stream/reverb). Wave
identity: the bank loader binds every `ALWaveTable*` to its pool entry (`dc_audio_bind_wave`); the driver keeps a
sorted array -> pool index (binary search at start). Command queue: per audio frame one batch wrapped in
`snd_sh4_to_aica_stop()` ... `snd_sh4_to_aica_start()`, timestamp 0: STOPs first, then STARTs, then UPDATEs with
`SET_FREQ|SET_VOL|SET_PAN` only for changed fields. A START is deferred to the flush so the SetPan/SetVol/SetPitch
issued in the same handler call travel with it. Music waves: acquire (LRU upload, refcount++) before START, release on
STOP/steal.

Mapping: libaudio vol s16 -> `(vol*vol) >> 22` (0..255); pan 0..127 -> `pan*2` (64 -> 128 centre); pitch ratio f32 ->
`hz = ratio * entry.rate_hz`, clamped to [low, FMAX], then the `fix_aica_freq_gap` correction. ADSR: `alSynSetVol(v,
vol, micros)` is a ramp; store `cur, tgt, frames`; each flush steps toward the target and emits SET_VOL when the mapped
byte changes (DIVERGES: linear per-frame steps instead of libaudio's sub-frame ramps). Steal: `WINGS64_AUDIO_STEAL_SOFT=1`
sets vol 0 this frame, STOP + new START next frame (DIVERGES: 16 ms delay). FXMix ignored (reverb off, DIVERGES).
Voice lifetimes are time-driven by the players, so no end-of-sample detection is needed.

## 6. Timing
`__amMain` already runs as a KOS thread fed by the scheduler's retrace message; keep it. Virtual AI in `os_shim.c`:
`osAiSetNextBuffer(buf, bytes)` adds `bytes/4` to `pending`; each vblank subtracts 367.5 samples (16.16 fixed);
`osAiGetLength` returns `max(pending,0)*4`. This reproduces the N64 closed loop: frameSamples floats around 368 and the
sum tracks wall-clock (tempo lock). If pending falls below `-CATCHUP_MAX*368` (thread starved), reset it to the nominal
lead and count `catchup_drop` (DIVERGES: drop time rather than fast-forward; `WINGS64_AUDIO_CATCHUP_MAX=4`).

## 7. Test plan (Flycast cannot hear)
Logs (`DB_AUDIO_LOG`, rate-limited): `aud: start v= ch= wave= fmt= hz= vol= pan=`, `aud: pool sfx= music_region= ram=`,
every 5 s `aud: stats frames= active= starts= steals= clamps= lru_miss= evict_playing= catchup= qfull=`;
`DB_AUDIO_SNAP` compares expected-playing vs `snd_is_playing(ch)` (KEYONB) -> `aud: ch_mismatch`. Dropout proxies:
`catchup_drop`, `evict_playing` (must be 0), `qfull`, `ch_mismatch`. Gate: `WINGS64_MILESTONE 4`; `WINGS64_M4_OK` at
the M3 flight->results point when music starts >= 1 in menu and in flight, SFX starts >= 20, a snapshot with KEYONB on
in menu and in flight, evict_playing == 0, catchup_drop <= 2. Soak: `DB_SOAK` loops for 600 s and prints
`WINGS64_M4_SOAK_OK`. Host tests `tools/audio/test_audio.py` (ROM-free, CI): Yamaha encode->decode SNR on sine/chirp,
loop invariants, decimation, table alignment/limits. Audio capture (optional): Flycast SDL2 backend with
`SDL_AUDIODRIVER=disk` into `build/emu-audio.raw`, then `tools/audio/rms_check.py` (100 ms RMS windows; fail on a
>= 100 ms silent gap in flight).

## 8. Kill switches (defaults = good build)
`WINGS64_AUDIO` 1 (0 = M3 silent build) · `WINGS64_AUDIO_MUSIC` 1 · `WINGS64_AUDIO_SFX` 1 · `WINGS64_AUDIO_MUSIC_LRU` 1
(0 = require fully resident) · `WINGS64_AUDIO_STEAL_SOFT` 1 · `WINGS64_AUDIO_RAMPS` 1 (0 = step volumes) ·
`WINGS64_AUDIO_BATCH` 1 (stop/start wrapping) · `WINGS64_AICA_ADPCM_FMAX` 88200 · `WINGS64_AUDIO_CATCHUP_MAX` 4 ·
`DB_AUDIO_LOG` 1 in db builds / 0 release · `DB_AUDIO_SNAP` 0 · transcoder `--no-decimate`, `--beam N`.

## Execution plan
0. Re-measure free RAM after the 4 MB arena; grep players for `->pvoice`; read `drvrnew.c`.
1. `tools/audio/vadpcm.py`, `yamaha.py`, `test_audio.py`. Check: tests pass without the ROM.
2. `tools/audio/transcode.py` -> `build/audio/wings64.aud` + report; wire into `run.sh disc/gate`; `pcm_force.txt`;
   `rom_leak_check.py` covers `*.aud`. Check: budget PASS, entries <= 65534 samples, 32-aligned.
3. Virtual AI + dummy Acmd. Check: `aud: rate` 22050 +/- 0.5% over 10 s in the emulator.
4. `src/dc/audio/aica_pool.c`: `snd_init`, load `.aud`, SFX upload, music region with LRU + refcount, wave binding.
5. `aica_synth.c` voice API, batches, mapping, ramps; exclude the libaudio TUs. Check: `aud: start` in menus.
6. Music LRU in the start path, steal path, clamp counter. Check: `evict_playing == 0`.
7. Gate: `milestone.h` -> 4, `WINGS64_M4_OK`, `DB_SOAK`. Check: `./run.sh gate` PASS, 10-min soak PASS, tests green.
8. Optional: SDL disk capture + `rms_check.py`; feed clamp logs into `pcm_force.txt`.
9. Hardware prep: switches + listening checklist in `run-hw` (high-SFX pitch, loop clicks, tempo drift, boot time).

## Risks
- ADPCM frequency ceiling is unverifiable in Flycast; wrong default = noise on high SFX on hardware; one-macro fix.
- G2 bandwidth: inline `spu_memload_sq` of a 30 KiB music wave in the audio frame (verify on hardware; mitigation:
  prefetch all music at boot when it fits).
- AICA ADPCM loops with non-zero loopstart depend on hardware decoder-state latching (verify; fallback PCM8).
- Key-on without envelopes may click on steal/stop; `STEAL_SOFT` mitigates steals only.
- Thread priority mapping and CD-R boot time (+1.9 MB) are hardware hypotheses until the burn.
- Per-sequence music sets were not measured; LRU thrash shows as `lru_miss` growth and `evict_playing` in the soak.

## Amendments from execution (M4, 2026-10-11)

1. **Everything resident in AICA RAM, no music paging (ruling 1).** The transcoder truncates looped waves at
   their loop end (libaudio loops count -1 never play the tail) and decimates the 5 waves still over 65534
   samples (sfx 0x14758, 0x44358, 0xe1300; music 0x62ab0, 0xbc460; sfx 0x3140 fits after truncation). The
   pool is then 1,860,032 B (sfx 1,193,632 + music 666,400) against 1,900,544 B of sample RAM, so the whole
   pool is uploaded at boot (~2 s in the emulator) and stays: no main-RAM music copy, no LRU, no
   `WINGS64_AUDIO_MUSIC_LRU`. `transcode.py` fails the build if the pool stops fitting (headroom 32 KiB);
   that is the moment to implement the LRU from ruling 1.
2. **No offline pitch analysis (ruling 3).** libaudio's resampler clamps the pitch ratio at `MAX_RATIO`
   1.99996 (+1 octave), which `alSynSetPitch` copies, so a 22050 Hz wave never plays above 44100 Hz and an
   11025 Hz one never above 22050 Hz. PCM is only by hand (`tools/audio/pcm_force.txt`); the runtime clamp
   counter stays as the hardware tripwire (`WINGS64_AICA_ADPCM_FMAX`).
3. **VADPCM decode is bit-exact**: for every looped ROM wave the decoded frame holding loopstart equals the
   bank's `ALADPCMloop.state` (`test_audio.py --rom`). **Yamaha ADPCM quality**: median 24.5 dB SNR, 46 of
   159 waves under 20 dB, worst 10.3 dB (music 0x52768, a saw-like wave whose hard edges the 4-bit step
   adaptation lags). A beam search gains ~1 dB, so the encoder stays greedy. Listening on hardware decides
   any `pcm_force.txt` entries (PCM8 doubles a wave's bytes; the budget has 32 KiB).
4. **Audio task off the scheduler (ruling 6, patches/022).** The audio thread waited for its placeholder
   task behind the scheduler's CPU display-list interpretation and missed ~7% of retraces (18% virtual-DAC
   underruns, ~20.4 kHz effective). `__amHandleFrameMsg` now completes the task itself. Raising the thread's
   priority above the scheduler measured no difference and was dropped. The virtual DAC carries a deficit
   of up to `WINGS64_AUDIO_CATCHUP_MAX` frames (osAiGetLength reads 0, the manager makes its longest frames)
   and reports `osAiGetLength` as the playing buffer only (the N64 AI does not count the queued one; counting
   it starves the loop by ~250 samples per frame).
5. **Title timing.** With real-time audio pacing `uvaSeqStop` waits ~0.9 s for voice releases at title
   music changes (controller polls pause; N64 semantics). M3's stub DAC ran the audio clock 1.32x fast
   (484 samples per frame), which hid it. The gate timeout went 150 -> 200 s because autoplay re-arms after
   such gaps.
6. **Kill switches as built**: `WINGS64_AUDIO` (0 = M3 silent path: libaudio synth linked, AI stubs),
   `WINGS64_AUDIO_MUSIC`, `WINGS64_AUDIO_SFX`, `WINGS64_AUDIO_STEAL_SOFT`, `WINGS64_AUDIO_RAMPS`,
   `WINGS64_AUDIO_BATCH`, `WINGS64_AICA_ADPCM_FMAX` (88200), `WINGS64_AUDIO_CATCHUP_MAX` (4), `DB_AUDIO_LOG`,
   `DB_SOAK`/`DB_SOAK_S`. Not built: `DB_AUDIO_SNAP` (the KEYONB snapshot runs every second in dev builds
   from the main thread instead), `WINGS64_AUDIO_MUSIC_LRU` (see 1).
7. **Gate numbers (emulator).** Rocket Belt gate: music 2194 key-ons in menus and 462 in flight, SFX 25, AICA
   KEYONB set in both, 0 wave misses, 0 audio-thread stall windows, 0% DAC drops, alAudioFrame 27-80 us
   per frame on average (max 29 ms once at boot: first sequence load). Soak 600 s: 0 misses, 0 stalls,
   13 drops in 35,820 frames. Flycast output captured (`EMU_AUDIO=1`, `tools/audio/rms_check.py`): -15..-26
   dBFS, no clipping; silent stretches only at level loads and music changes, where the players hold no
   voices (`rms_check --gap-max` is only meaningful over a stretch known to play sound).
