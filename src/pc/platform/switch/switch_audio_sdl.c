#ifdef __SWITCH__

#include <stdio.h>
#include <SDL2/SDL.h>

#include "pc/audio/audio_api.h"

static SDL_AudioDeviceID sDevice;

static bool switch_audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL audio init error: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    want.freq = 32000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = NULL;

    sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sDevice == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice error: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_PauseAudioDevice(sDevice, 0);
    return true;
}

static int switch_audio_buffered(void) {
    return sDevice ? (int)(SDL_GetQueuedAudioSize(sDevice) / 4) : 0;
}

static int switch_audio_desired_buffered(void) {
    return 1100;
}

static void switch_audio_play(const uint8_t *buf, size_t len) {
    if (!sDevice) return;

    /* Keep latency bounded if rendering stalls for a few frames. */
    if (SDL_GetQueuedAudioSize(sDevice) < 24000) {
        SDL_QueueAudio(sDevice, buf, (Uint32)len);
    }
}

static void switch_audio_shutdown(void) {
    if (sDevice) {
        SDL_ClearQueuedAudio(sDevice);
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

struct AudioAPI audio_sdl = {
    switch_audio_init,
    switch_audio_buffered,
    switch_audio_desired_buffered,
    switch_audio_play,
    switch_audio_shutdown,
};

#endif
