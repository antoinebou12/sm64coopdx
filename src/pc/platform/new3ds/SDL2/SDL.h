#pragma once

#ifdef __3DS__

/*
 * pc_main.c only needs SDL_DisplayMode/SDL_GetCurrentDisplayMode to determine
 * a refresh cap. The New 3DS top LCD refreshes at 60 Hz, so avoid pulling SDL
 * into the native platform build for this one desktop-oriented query.
 */
typedef struct SDL_DisplayMode {
    int refresh_rate;
} SDL_DisplayMode;

static inline int SDL_GetCurrentDisplayMode(int display_index, SDL_DisplayMode *mode) {
    (void)display_index;
    if (mode != 0) mode->refresh_rate = 60;
    return 0;
}

#else
#error "The New 3DS SDL compatibility shim must only be used with __3DS__"
#endif
