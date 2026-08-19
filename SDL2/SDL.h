#pragma once

#ifdef __SWITCH__
#include_next <SDL2/SDL.h>
#else
#include <SDL3/SDL.h>

/*
 * Transitional SDL2 include compatibility for current dev code while the
 * desktop backend moves to SDL3. Keep Switch on devkitPro's SDL2 portlib.
 */
static inline int sm64_sdl2_get_current_display_mode(int display_index, SDL_DisplayMode *mode) {
    (void)display_index;
    if (!mode) { return -1; }

    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display) { return -1; }

    const SDL_DisplayMode *current = SDL_GetCurrentDisplayMode(display);
    if (!current) { return -1; }

    *mode = *current;
    return 0;
}

#define SDL_GetCurrentDisplayMode(display_index, mode) \
    sm64_sdl2_get_current_display_mode((display_index), (mode))
#endif
