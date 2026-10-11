/* aica_synth.h — counters and hooks of the libaudio synthesizer replacement (aica_synth.c).
 * Plain C types so port code (main.c) and decomp-flag code (pw64_audio_load.c) can both include it. */
#ifndef WINGS64_AICA_SYNTH_H
#define WINGS64_AICA_SYNTH_H

typedef struct {
    unsigned frames;          /* alAudioFrame calls that ran players */
    unsigned starts[2];       /* key-ons by bank: [0] SFX, [1] music */
    unsigned steals;          /* voices taken from a lower-priority owner */
    unsigned clamps;          /* ADPCM pitch above WINGS64_AICA_ADPCM_FMAX (clamped) */
    unsigned misses;          /* starts whose wave is not in the pool */
    unsigned active;          /* channels keyed on now (driver view) */
    unsigned peak_active;
    unsigned ai_underruns;    /* virtual AI DAC deficit beyond WINGS64_AUDIO_CATCHUP_MAX frames: time dropped (os_shim.c) */
    unsigned long long ai_samples; /* samples handed to the virtual DAC */
    unsigned us_max, us_sum;  /* alAudioFrame CPU time: worst and total since the last read (us) */
    unsigned us_frames;
} aud_stats_t;

void aud_synth_stats(aud_stats_t *out);
void aud_synth_cpu_reset(void);   /* restart the us_* window */
/* Channels the driver believes keyed on vs the hardware KEYONB bits (DB_AUDIO_SNAP); returns mismatches. */
unsigned aud_synth_snapshot(unsigned *expected, unsigned *hw);
/* The bank loader reports each .TBL it binds so ALWaveTable bases map back to pool entries. */
void aud_synth_bank_table(const void *table);

#endif
