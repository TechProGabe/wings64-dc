/* aica_synth.c — libaudio's synthesis driver re-implemented on AICA channels (docs/audio-design.md §5).
 *
 * The players (ALCSPlayer music, ALSndPlayer SFX) are the decomp's own and run unchanged in __amMain.
 * They talk to the synthesizer only through the alSyn* voice API, which this file provides in place of
 * libultra/audio/synthesizer.c + syn*.c + the RSP filter chain (excluded from the build, src/game/Makefile).
 * Physical voice i owns AICA channel i (32 voices, channels 32-63 unused). Parameter changes collected
 * during a frame's player callbacks go out as one command batch at the end of alAudioFrame.
 *
 * DIVERGES: N64 parameter updates land at sub-frame sample offsets and volume ramps are evaluated per
 * RSP sub-frame; here they apply at audio-frame granularity (one per retrace, ~16.7 ms), ramps are linear
 * steps per frame. A stolen voice is silenced one frame before the new note keys on (N64: 448-sample
 * ramp, 512-sample delay). No reverb/FX bus (alSynSetFXMix and the AL_FX_CUSTOM params are ignored).
 */
#include <ultra64.h>
#include <PR/libaudio.h>
#include "aica_drv.h"
#include "aica_synth.h"
#include "../ultra/kos_bridge.h"

#ifndef WINGS64_AUDIO_RAMPS
#define WINGS64_AUDIO_RAMPS 1
#endif
#ifndef WINGS64_AUDIO_STEAL_SOFT
#define WINGS64_AUDIO_STEAL_SOFT 1
#endif
#ifndef WINGS64_AUDIO_MUSIC
#define WINGS64_AUDIO_MUSIC 1
#endif
#ifndef WINGS64_AUDIO_SFX
#define WINGS64_AUDIO_SFX 1
#endif
/* Highest playback rate for a Yamaha ADPCM wave (AICA: no more than +1 octave over 44.1 kHz). The N64
   resampler already caps pitch at MAX_RATIO (+1 octave over the wave's rate), so with 22050 Hz waves
   this never trips unless hardware proves the ceiling lower (A/B 44100 on the console). */
#ifndef WINGS64_AICA_ADPCM_FMAX
#define WINGS64_AICA_ADPCM_FMAX 88200
#endif
#ifndef DB_AUDIO_LOG
#define DB_AUDIO_LOG 1
#endif

#define NVOICES 32
#define RSP_SAMPLES 160                      /* synthInternals.h AL_MAX_RSP_SAMPLES */
#define PITCH_MAX 1.99996f                   /* abi.h MAX_RATIO, as a float */
#define AUDIO_TBL_ROM 0x006314D0u
#define AUDIO_TBL_SPAN 0x00200000u

enum { V_IDLE, V_START, V_ON };

typedef struct {
    ALVoice *vv;                 /* owner, NULL = free */
    const aud_wave_t *wave;
    s32 volCur, volTgt;          /* libaudio voice volume 0..32767, before the envmixer's square */
    s32 rampLeft;                /* samples left in the volume ramp */
    f32 pitch;
    u8 state, stopReq, hold, pan;
    u8 on;                       /* channel keyed on (driver view) */
    u8 sentVol, sentPan;
    u32 sentHz;
} dc_voice_t;

static dc_voice_t sVoice[NVOICES];
static const u8 *sSfxTable;
static aud_stats_t sStats;

void aud_synth_bank_table(const void *table) {
    if ((u32)table - AUDIO_TBL_ROM >= AUDIO_TBL_SPAN) sSfxTable = table;   /* the RAM-resident UVSX .TBL */
}

void aud_synth_stats(aud_stats_t *out) {
    extern u32 wings64_ai_underruns(void);
    extern unsigned long long wings64_ai_samples(void);
    sStats.ai_underruns = wings64_ai_underruns();
    sStats.ai_samples = wings64_ai_samples();
    *out = sStats;
}

void aud_synth_cpu_reset(void) { sStats.us_max = sStats.us_sum = sStats.us_frames = 0; }

unsigned aud_synth_snapshot(unsigned *expected, unsigned *hw) {
    unsigned e = 0, h = 0, bad = 0;
    for (int i = 0; i < NVOICES; i++) {
        int k = aud_drv_keyon(i);
        e += sVoice[i].on;
        h += k != 0;
        bad += (sVoice[i].on != 0) != (k != 0);
    }
    *expected = e;
    *hw = h;
    return bad;
}

static const aud_wave_t *find_wave(const ALWaveTable *w) {
    u32 base = (u32)w->base;
    if (base - AUDIO_TBL_ROM < AUDIO_TBL_SPAN) {
        return aud_drv_find(AUD_BANK_MUSIC, base - AUDIO_TBL_ROM, (u32)w->len);
    }
    if (sSfxTable) return aud_drv_find(AUD_BANK_SFX, base - (u32)sSfxTable, (u32)w->len);
    return NULL;
}

static s32 time_to_samples(ALSynth *s, s32 micros) {
    /* synthesizer.c _timeToSamplesNoRound in integers (no double on SH-4) */
    return (s32)(((s64)micros * s->outputRate + 500000) / 1000000);
}

static dc_voice_t *slot_of(ALVoice *v) { return (dc_voice_t *)v->pvoice; }

/* ---------------------------------------------------------------- driver lifecycle */

void alSynNew(ALSynth *drvr, ALSynConfig *c) {
    drvr->head = NULL;
    drvr->numPVoices = c->maxPVoices < NVOICES ? c->maxPVoices : NVOICES;
    drvr->curSamples = 0;
    drvr->paramSamples = 0;
    drvr->outputRate = c->outputRate;
    drvr->maxOutSamples = RSP_SAMPLES;
    drvr->dma = (ALDMANew)c->dmaproc;
    drvr->heap = c->heap;
    for (int i = 0; i < NVOICES; i++) {
        sVoice[i] = (dc_voice_t){ 0 };
        sVoice[i].pitch = 1.0f;
        sVoice[i].pan = AL_PAN_CENTER;
    }
    kb_log("aud: synth %d voices -> AICA channels 0-%d, %lu Hz, pool %s\n", (int)drvr->numPVoices,
           (int)drvr->numPVoices - 1, (unsigned long)drvr->outputRate, aud_drv_ready() ? "ready" : "MISSING");
}

void alSynDelete(ALSynth *drvr) {
    aud_drv_batch_begin();
    for (int i = 0; i < NVOICES; i++) {
        if (sVoice[i].on) aud_drv_stop(i);
        sVoice[i].on = 0;
        sVoice[i].state = V_IDLE;
    }
    aud_drv_batch_end();
    drvr->head = NULL;
}

ALFxRef *alSynAllocFX(ALSynth *s, s16 bus, ALSynConfig *c, ALHeap *hp) { return NULL; }
ALFxRef alSynGetFXRef(ALSynth *s, s16 bus, s16 index) { return NULL; }
void alSynFreeFX(ALSynth *s, ALFxRef *fx) {}
void alSynSetFXParam(ALSynth *s, ALFxRef fx, s16 paramID, void *param) {}

/* ---------------------------------------------------------------- voices */

s32 alSynAllocVoice(ALSynth *drvr, ALVoice *voice, ALVoiceConfig *vc) {
    dc_voice_t *pick = NULL;
    s16 prio = vc->priority;
    int stolen = 0;

    voice->priority = vc->priority;
    voice->unityPitch = vc->unityPitch;
    voice->table = 0;
    voice->fxBus = vc->fxBus;
    voice->state = AL_STOPPED;
    voice->pvoice = 0;

    for (int i = 0; i < drvr->numPVoices && !pick; i++)
        if (!sVoice[i].vv && !sVoice[i].hold) pick = &sVoice[i];
    if (!pick) {
        /* synallocvoice.c _allocatePVoice: the lowest-priority voice at or below ours, not already stolen */
        for (int i = 0; i < drvr->numPVoices; i++) {
            dc_voice_t *p = &sVoice[i];
            if (p->vv && !p->hold && p->vv->priority <= prio) {
                pick = p;
                prio = p->vv->priority;
                stolen = 1;
            }
        }
    }
    if (!pick) return 0;
    if (stolen) {
        pick->vv->pvoice = 0;
        pick->hold = WINGS64_AUDIO_STEAL_SOFT;
        pick->stopReq = !WINGS64_AUDIO_STEAL_SOFT;
        sStats.steals++;
    }
    pick->vv = voice;
    pick->state = V_IDLE;
    voice->pvoice = (struct PVoice_s *)pick;
    return 1;
}

void alSynFreeVoice(ALSynth *drvr, ALVoice *voice) {
    dc_voice_t *p = slot_of(voice);
    if (p) {
        if (p->state == V_START) p->state = V_IDLE;
        if (p->on) p->stopReq = 1;
        p->vv = NULL;
        voice->pvoice = 0;
    }
}

static void start_voice(ALVoice *voice, ALWaveTable *w) {
    dc_voice_t *p = slot_of(voice);
    if (!p) return;
    voice->table = w;
    p->wave = find_wave(w);
    if (!p->wave) {
        sStats.misses++;
        if (sStats.misses <= 8)
            kb_log("aud: miss wave base %08lx len %ld\n", (unsigned long)(u32)w->base, (long)w->len);
        return;
    }
    if ((p->wave->bank == AUD_BANK_MUSIC && !WINGS64_AUDIO_MUSIC) || (p->wave->bank == AUD_BANK_SFX && !WINGS64_AUDIO_SFX))
        return;
    p->state = V_START;
    p->volCur = 0;
    p->volTgt = 0;
    p->rampLeft = 0;
}

void alSynStartVoice(ALSynth *drvr, ALVoice *voice, ALWaveTable *w) { start_voice(voice, w); }

void alSynStartVoiceParams(ALSynth *drvr, ALVoice *voice, ALWaveTable *w, f32 pitch, s16 vol, ALPan pan, u8 fxmix,
                           ALMicroTime t) {
    dc_voice_t *p = slot_of(voice);
    start_voice(voice, w);
    if (!p) return;
    p->pitch = pitch;
    p->pan = pan;
    p->volTgt = vol;
    p->rampLeft = time_to_samples(drvr, t);
    if (p->rampLeft <= 0 || !WINGS64_AUDIO_RAMPS) {
        p->volCur = vol;
        p->rampLeft = 0;
    }
}

void alSynStopVoice(ALSynth *drvr, ALVoice *voice) {
    dc_voice_t *p = slot_of(voice);
    if (!p) return;
    if (p->state == V_START) p->state = V_IDLE;
    if (p->on) p->stopReq = 1;
}

void alSynSetVol(ALSynth *drvr, ALVoice *voice, s16 vol, ALMicroTime t) {
    dc_voice_t *p = slot_of(voice);
    if (!p) return;
    p->volTgt = vol;
    p->rampLeft = time_to_samples(drvr, t);
    if (p->rampLeft <= 0 || !WINGS64_AUDIO_RAMPS) {
        p->volCur = vol;
        p->rampLeft = 0;
    }
}

void alSynSetPitch(ALSynth *drvr, ALVoice *voice, f32 ratio) {
    dc_voice_t *p = slot_of(voice);
    if (p) p->pitch = ratio > PITCH_MAX ? PITCH_MAX : ratio;   /* resample.c clamps the same way */
}

void alSynSetPan(ALSynth *drvr, ALVoice *voice, ALPan pan) {
    dc_voice_t *p = slot_of(voice);
    if (p) p->pan = pan;
}

void alSynSetFXMix(ALSynth *drvr, ALVoice *voice, u8 fxmix) {}

void alSynSetPriority(ALSynth *drvr, ALVoice *voice, s16 priority) { voice->priority = priority; }
s16 alSynGetPriority(ALSynth *drvr, ALVoice *voice) { return voice->priority; }

/* ---------------------------------------------------------------- frame */

static u32 voice_hz(dc_voice_t *p, int ch) {
    f32 f = p->pitch * (f32)p->wave->rate + 0.5f;
    u32 hz = f < 1.0f ? 1u : (u32)f;
    if (p->wave->fmt == AUD_FMT_ADPCM && hz > WINGS64_AICA_ADPCM_FMAX) {
        if (sStats.clamps++ < 16)
            kb_log("aud: clamp ch %d wave %d hz %lu\n", ch, aud_drv_wave_index(p->wave), (unsigned long)hz);
        hz = WINGS64_AICA_ADPCM_FMAX;
    }
    return hz;
}

/* env.c maps volume through a square (vol*vol >> 15) and pans with an equal-power table that puts the
   centre at -3 dB per side; AICA vol is linear amplitude 0..255 and its pan keeps the near side at full
   level, so the centre gain 181/256 matches the N64's centred loudness. */
static u8 voice_vol(const dc_voice_t *p) {
    s32 v = p->volCur < 0 ? 0 : p->volCur;
    u32 amp = ((u32)v * (u32)v) >> 22;          /* 32767^2 >> 22 = 255 */
    amp = (amp * 181u) >> 8;
    return (u8)(amp > 255 ? 255 : amp);
}

static u8 voice_pan(const dc_voice_t *p) { return (u8)(p->pan >= 127 ? 255 : p->pan * 2); }

static void flush(s32 outLen) {
    unsigned active = 0;
    aud_drv_batch_begin();
    for (int ch = 0; ch < NVOICES; ch++) {
        dc_voice_t *p = &sVoice[ch];
        if (p->hold) {                       /* stolen: silence now, the new owner keys on next frame */
            if (p->on) aud_drv_update(ch, p->sentHz, 0, p->sentPan);
            p->hold = 0;
            continue;
        }
        if (p->stopReq) {
            if (p->on) aud_drv_stop(ch);
            p->on = 0;
            p->stopReq = 0;
        }
        if (p->rampLeft > 0) {
            s32 step = outLen < p->rampLeft ? outLen : p->rampLeft;
            p->volCur += (s32)(((s64)(p->volTgt - p->volCur) * step) / p->rampLeft);
            p->rampLeft -= step;
        }
        if (p->state == V_START && p->wave) {
            u32 hz = voice_hz(p, ch);
            u8 vol = voice_vol(p), pan = voice_pan(p);
            aud_drv_start(ch, p->wave, hz, vol, pan);
            p->on = 1;
            p->state = V_ON;
            p->sentHz = hz;
            p->sentVol = vol;
            p->sentPan = pan;
            sStats.starts[p->wave->bank & 1]++;
#if DB_AUDIO_LOG
            /* every wave's first key-on (names it for pcm_force.txt), then one start in 256 */
            static u8 sSeen[256];
            int wi = aud_drv_wave_index(p->wave);
            int first = wi >= 0 && wi < 256 && !sSeen[wi];
            if (first) sSeen[wi] = 1;
            if (first || ((sStats.starts[0] + sStats.starts[1]) & 255) == 0)
                kb_log("aud: start ch %d wave %d (%s:0x%x) fmt %d hz %lu vol %u pan %u\n", ch, wi,
                       p->wave->bank ? "mus" : "sfx", p->wave->base, p->wave->fmt,
                       (unsigned long)hz, vol, pan);
#endif
        } else if (p->on) {
            u32 hz = voice_hz(p, ch);
            u8 vol = voice_vol(p), pan = voice_pan(p);
            if (hz != p->sentHz || vol != p->sentVol || pan != p->sentPan) {
                aud_drv_update(ch, hz, vol, pan);
                p->sentHz = hz;
                p->sentVol = vol;
                p->sentPan = pan;
            }
        }
        active += p->on;
    }
    aud_drv_batch_end();
    sStats.active = active;
    if (active > sStats.peak_active) sStats.peak_active = active;
}

/* synthesizer.c alAudioFrame minus the command-list build: run the players' callbacks due this frame,
   advance the sample clock, send the frame's channel commands. One dummy Acmd keeps *cmdLen non-zero:
   audio_manager.c only queues the next AI buffer (the virtual DAC that paces frameSamples) after a task. */
Acmd *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen) {
    ALSynth *drvr = &alGlobals->drvr;
    ALPlayer *client;
    unsigned long long t0 = kb_time_ns();
    unsigned us;

    if (drvr->head == 0) {
        *cmdLen = 0;
        return cmdList;
    }
    for (;;) {
        ALMicroTime delta = 0x7fffffff;
        client = 0;
        for (ALPlayer *cl = drvr->head; cl != 0; cl = cl->next) {
            if ((cl->samplesLeft - drvr->curSamples) < delta) {
                client = cl;
                delta = cl->samplesLeft - drvr->curSamples;
            }
        }
        drvr->paramSamples = client->samplesLeft;
        if (drvr->paramSamples - drvr->curSamples >= outLen) break;
        drvr->paramSamples &= ~0xf;
        client->samplesLeft += time_to_samples(drvr, (*client->handler)(client));
    }
    drvr->paramSamples &= ~0xf;
    drvr->curSamples += outLen;
    flush(outLen);
    sStats.frames++;
    us = (unsigned)((kb_time_ns() - t0) / 1000u);
    sStats.us_sum += us;
    sStats.us_frames++;
    if (us > sStats.us_max) sStats.us_max = us;
    cmdList[0].words.w0 = 0;
    cmdList[0].words.w1 = 0;
    *cmdLen = 1;
    return cmdList + 1;
}
