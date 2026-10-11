/* aica_drv.h — AICA side of the audio port (docs/audio-design.md): the resident sample pool and the
 * KOS SH-4 -> ARM channel command queue. aica_synth.c (compiled with the decomp's headers) drives it,
 * so this header uses plain C types and includes nothing.
 */
#ifndef WINGS64_AICA_DRV_H
#define WINGS64_AICA_DRV_H

#define AUD_BANK_SFX 0
#define AUD_BANK_MUSIC 1
#define AUD_FMT_ADPCM 0
#define AUD_FMT_PCM8 1
#define AUD_FMT_PCM16 2
#define AUD_NO_LOOP 0xFFFFFFFFu
#define AUD_CHANNELS 64

/* One wave of disc/wings64.aud (tools/audio/transcode.py); same 32-byte layout as the file. */
typedef struct {
    unsigned char bank, fmt;
    unsigned short rate;     /* 22050, or 11025 for waves decimated to fit 65534 samples */
    unsigned base;           /* offset of the VADPCM wave in its .TBL (ALWaveTable base - table) */
    unsigned vlen;           /* VADPCM bytes (ALWaveTable len) */
    unsigned off, bytes;     /* in the bank's blob */
    unsigned samples;        /* play end: loop end, or the whole wave */
    unsigned loopstart, loopend;
} aud_wave_t;

/* snd_init + upload the whole pool from <root>/wings64.aud into AICA RAM. 0 = ok. */
int aud_drv_init(const char *root);
int aud_drv_ready(void);
unsigned aud_drv_pool_bytes(void);
unsigned aud_drv_wave_count(void);

/* Wave for an ALWaveTable: bank, offset in its .TBL, VADPCM length. NULL if not in the pool. */
const aud_wave_t *aud_drv_find(int bank, unsigned off, unsigned vlen);
int aud_drv_wave_index(const aud_wave_t *w);

/* One batch of channel commands per audio frame (WINGS64_AUDIO_BATCH: the ARM sees it at once). */
void aud_drv_batch_begin(void);
void aud_drv_batch_end(void);
/* vol 0..255 linear amplitude (the firmware maps it to attenuation), pan 0..255 (128 centre), hz playback rate. */
void aud_drv_start(int ch, const aud_wave_t *w, unsigned hz, unsigned vol, unsigned pan);
void aud_drv_update(int ch, unsigned hz, unsigned vol, unsigned pan);
void aud_drv_stop(int ch);
/* Hardware key-on bit (KEYONB) of a channel: 1 while the channel plays. */
int aud_drv_keyon(int ch);
unsigned aud_drv_packets(void);

#endif
