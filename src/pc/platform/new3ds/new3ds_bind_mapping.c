#ifdef __3DS__

#include <stdio.h>

#include "pc/controller/controller_api.h"
#include "pc/controller/controller_bind_mapping.h"
#include "pc/controller/controller_sdl.h"

/*
 * CoopDX stores its default controller bindings in the SDL gamepad virtual-key
 * range even when the platform does not use SDL. Keep those persisted values
 * readable on New 3DS without dragging SDL3 into the handheld build.
 */
void controller_bind_init(void) {
}

int translate_sdl_scancode(int scancode) {
    (void)scancode;
    return VK_INVALID;
}

const char *translate_bind_to_name(int bind) {
    static char fallback[12];

    if (bind == VK_INVALID) return "";

    if (bind >= VK_BASE_SDL_GAMEPAD && bind < VK_BASE_SDL_MOUSE) {
        switch (bind - VK_BASE_SDL_GAMEPAD) {
            case 0: return "[A]";
            case 1: return "[B]";
            case 2: return "[X]";
            case 3: return "[Y]";
            case 4: return "[Select]";
            case 6: return "[Start]";
            case 9: return "[L]";
            case 10: return "[R]";
            case 11: return "[D-Up]";
            case 12: return "[D-Down]";
            case 13: return "[D-Left]";
            case 14: return "[D-Right]";
            case (VK_LTRIGGER - VK_BASE_SDL_GAMEPAD): return "[ZL]";
            case (VK_RTRIGGER - VK_BASE_SDL_GAMEPAD): return "[ZR]";
            default: break;
        }
    }

    snprintf(fallback, sizeof(fallback), "%04X", bind & 0xFFFF);
    return fallback;
}

#endif /* __3DS__ */
