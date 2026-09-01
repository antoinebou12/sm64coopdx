#ifdef __3DS__

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "audio_new3ds.h"

#define NEW3DS_DSP_BUFFER_COUNT 4
#define NEW3DS_DSP_BUFFER_SAMPLES 4096
#define NEW3DS_DSP_BYTES_PER_SAMPLE_FRAME 4
#define NEW3DS_DSP_BUFFER_BYTES \
    (NEW3DS_DSP_BUFFER_SAMPLES * NEW3DS_DSP_BYTES_PER_SAMPLE_FRAME)

static ndspWaveBuf sWaveBuffers[NEW3DS_DSP_BUFFER_COUNT];
static uint8_t *sBufferData = NULL;
static int sNextBuffer = 0;
static bool sInitialized = false;

static bool audio_new3ds_init(void) {
    if (sInitialized) return true;

    Result rc = ndspInit();
    if (R_FAILED(rc)) return false;

    sBufferData = (uint8_t *)linearAlloc(
        NEW3DS_DSP_BUFFER_BYTES * NEW3DS_DSP_BUFFER_COUNT);
    if (sBufferData == NULL) {
        ndspExit();
        return false;
    }

    memset(sWaveBuffers, 0, sizeof(sWaveBuffers));
    for (int i = 0; i < NEW3DS_DSP_BUFFER_COUNT; ++i) {
        sWaveBuffers[i].data_vaddr =
            sBufferData + (i * NEW3DS_DSP_BUFFER_BYTES);
        sWaveBuffers[i].status = NDSP_WBUF_FREE;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnWaveBufClear(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, 32000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] = {0};
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    sNextBuffer = 0;
    sInitialized = true;
    return true;
}

static int audio_new3ds_buffered(void) {
    if (!sInitialized) return 0;

    int total = 0;
    for (int i = 0; i < NEW3DS_DSP_BUFFER_COUNT; ++i) {
        if (sWaveBuffers[i].status == NDSP_WBUF_QUEUED ||
            sWaveBuffers[i].status == NDSP_WBUF_PLAYING) {
            total += (int)sWaveBuffers[i].nsamples;
        }
    }
    return total;
}

static int audio_new3ds_get_desired_buffered(void) {
    return 1100;
}

static void audio_new3ds_play(const uint8_t *buf, size_t len) {
    if (!sInitialized || buf == NULL || len == 0 || len > NEW3DS_DSP_BUFFER_BYTES) {
        return;
    }

    ndspWaveBuf *wave = &sWaveBuffers[sNextBuffer];
    if (wave->status != NDSP_WBUF_FREE && wave->status != NDSP_WBUF_DONE) {
        return;
    }

    memcpy(wave->data_vaddr, buf, len);
    DSP_FlushDataCache(wave->data_vaddr, len);
    wave->nsamples = (u32)(len / NEW3DS_DSP_BYTES_PER_SAMPLE_FRAME);
    wave->status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(0, wave);

    sNextBuffer = (sNextBuffer + 1) % NEW3DS_DSP_BUFFER_COUNT;
}

static void audio_new3ds_shutdown(void) {
    if (!sInitialized) return;

    ndspChnWaveBufClear(0);
    ndspChnReset(0);
    ndspExit();

    if (sBufferData != NULL) {
        linearFree(sBufferData);
        sBufferData = NULL;
    }

    memset(sWaveBuffers, 0, sizeof(sWaveBuffers));
    sNextBuffer = 0;
    sInitialized = false;
}

struct AudioAPI audio_new3ds = {
    audio_new3ds_init,
    audio_new3ds_buffered,
    audio_new3ds_get_desired_buffered,
    audio_new3ds_play,
    audio_new3ds_shutdown,
};

#endif /* __3DS__ */
