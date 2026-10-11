/* aica_drv.c — AICA sample pool + channel commands on the stock KOS v2.2.2 sound firmware.
 *
 * The pool (tools/audio/transcode.py -> disc/wings64.aud) is uploaded whole at boot: SFX blob then music
 * blob, contiguous in one snd_mem_malloc region (1.77 MiB of the 1.81 MiB sample RAM; the transcoder's
 * budget check guarantees the fit). Channel commands are AICA_CMD_CHAN packets with timestamp 0.
 * DIVERGES: no reverb (the N64 AL_FX_CUSTOM aux bus is not reproduced in M4).
 */
#include "aica_drv.h"
#include "db.h"

#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/aica_comm.h>
#include <dc/spu.h>
#include <malloc.h>
#include <string.h>

#ifndef WINGS64_AUDIO_BATCH
#define WINGS64_AUDIO_BATCH 1
#endif

#define AUD_MAGIC 0x41343657u   /* "W64A" little-endian */
#define AUD_VERSION 1u
#define AUD_MAX_WAVES 256
#define BOUNCE 32768

typedef struct {
    uint32_t magic, version, n, sfx_off, sfx_bytes, mus_off, mus_bytes;
} aud_hdr_t;

_Static_assert(sizeof(aud_wave_t) == 32, "wings64.aud entry is 32 bytes");

static aud_wave_t sWaves[AUD_MAX_WAVES];
static unsigned sNumWaves;
static uint32_t sPoolBase, sPoolBytes, sMusBase, sSfxBase;
static int sReady;
static unsigned sPackets;

static int read_all(file_t f, void *dst, size_t n) {
    return fs_read(f, dst, n) == (ssize_t)n ? 0 : -1;
}

int aud_drv_init(const char *root) {
    char path[128];
    aud_hdr_t h;
    file_t f;
    uint8_t *bounce;
    uint32_t total, done = 0;
    uint64_t t0 = timer_ms_gettime64();

    snprintf(path, sizeof path, "%s/wings64.aud", root);
    f = fs_open(path, O_RDONLY);
    if (f < 0) {
        db_log("aud: %s missing; audio off\n", path);
        return -1;
    }
    if (read_all(f, &h, sizeof h) || h.magic != AUD_MAGIC || h.version != AUD_VERSION || h.n > AUD_MAX_WAVES ||
        read_all(f, sWaves, h.n * sizeof(aud_wave_t)) || h.mus_off != h.sfx_off + h.sfx_bytes) {
        db_log("aud: bad wings64.aud header (magic %08lx v%lu n %lu)\n", (unsigned long)h.magic,
               (unsigned long)h.version, (unsigned long)h.n);
        fs_close(f);
        return -1;
    }
    sNumWaves = h.n;
    total = h.sfx_bytes + h.mus_bytes;

    snd_init();
    sPoolBase = snd_mem_malloc(total);
    if (!sPoolBase) {
        db_log("aud: AICA pool %lu B does not fit (free %lu B)\n", (unsigned long)total,
               (unsigned long)snd_mem_available());
        fs_close(f);
        return -1;
    }
    sSfxBase = sPoolBase;
    sMusBase = sPoolBase + h.sfx_bytes;
    sPoolBytes = total;

    bounce = memalign(32, BOUNCE);
    if (!bounce || fs_seek(f, h.sfx_off, SEEK_SET) < 0) {
        fs_close(f);
        free(bounce);
        return -1;
    }
    while (done < total) {
        uint32_t n = total - done < BOUNCE ? total - done : BOUNCE;   /* blobs are 32-byte multiples */
        if (read_all(f, bounce, n)) {
            db_log("aud: read failed at %lu/%lu\n", (unsigned long)done, (unsigned long)total);
            fs_close(f);
            free(bounce);
            return -1;
        }
        spu_memload_sq(sPoolBase + done, bounce, n);
        done += n;
    }
    fs_close(f);
    free(bounce);
    snd_sh4_to_aica_start();
    sReady = 1;
    db_log("aud: pool %lu B (sfx %lu + music %lu) at AICA %06lx, %u waves, %lu ms; AICA free %lu B\n",
           (unsigned long)total, (unsigned long)h.sfx_bytes, (unsigned long)h.mus_bytes, (unsigned long)sPoolBase,
           sNumWaves, (unsigned long)(timer_ms_gettime64() - t0), (unsigned long)snd_mem_available());
    return 0;
}

int aud_drv_ready(void) { return sReady; }
unsigned aud_drv_pool_bytes(void) { return sPoolBytes; }
unsigned aud_drv_wave_count(void) { return sNumWaves; }
unsigned aud_drv_packets(void) { return sPackets; }

/* Entries are sorted by (bank, base, vlen) by the transcoder. */
const aud_wave_t *aud_drv_find(int bank, unsigned off, unsigned vlen) {
    int lo = 0, hi = (int)sNumWaves - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const aud_wave_t *w = &sWaves[mid];
        int c = w->bank != bank ? (w->bank < bank ? -1 : 1)
              : w->base != off ? (w->base < off ? -1 : 1)
              : w->vlen != vlen ? (w->vlen < vlen ? -1 : 1) : 0;
        if (c == 0) return w;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int aud_drv_wave_index(const aud_wave_t *w) { return w ? (int)(w - sWaves) : -1; }

void aud_drv_batch_begin(void) {
#if WINGS64_AUDIO_BATCH
    if (sReady) snd_sh4_to_aica_stop();
#endif
}

void aud_drv_batch_end(void) {
#if WINGS64_AUDIO_BATCH
    if (sReady) snd_sh4_to_aica_start();
#endif
}

/* The firmware's frequency encoder has no exponent above +7 (5644800 * 2); keep the mantissa in range
   (sm64-dc fix_aica_freq_gap). Pitches here stay far below it. */
static uint32_t clamp_freq(uint32_t hz) {
    if (hz < 1) hz = 1;
    if (hz >= 2u * 5644800u) hz = 2u * 5644800u - 1u;
    return hz;
}

static void chan_cmd(int ch, uint32_t cmdword, const aud_wave_t *w, uint32_t hz, uint32_t vol, uint32_t pan) {
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
    memset(tmp, 0, sizeof tmp);
    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = (uint32_t)ch;
    chan->cmd = cmdword;
    if (w) {
        static const uint32_t kType[3] = { AICA_SM_ADPCM, AICA_SM_8BIT, AICA_SM_16BIT };
        chan->base = (w->bank == AUD_BANK_SFX ? sSfxBase : sMusBase) + w->off;
        chan->type = kType[w->fmt < 3 ? w->fmt : 0];
        chan->length = w->samples;
        chan->loop = w->loopstart != AUD_NO_LOOP;
        chan->loopstart = chan->loop ? w->loopstart : 0;
        chan->loopend = w->samples;
    }
    chan->freq = clamp_freq(hz);
    chan->vol = vol > 255 ? 255 : vol;
    chan->pan = pan > 255 ? 255 : pan;
    snd_sh4_to_aica(tmp, cmd->size);
    sPackets++;
}

void aud_drv_start(int ch, const aud_wave_t *w, unsigned hz, unsigned vol, unsigned pan) {
    if (sReady && w) chan_cmd(ch, AICA_CH_CMD_START, w, hz, vol, pan);
}

void aud_drv_update(int ch, unsigned hz, unsigned vol, unsigned pan) {
    if (sReady)
        chan_cmd(ch, AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN, NULL,
             hz, vol, pan);
}

void aud_drv_stop(int ch) {
    if (sReady) chan_cmd(ch, AICA_CH_CMD_STOP, NULL, 1, 0, 128);
}

int aud_drv_keyon(int ch) { return sReady ? snd_is_playing((unsigned)ch) : 0; }
