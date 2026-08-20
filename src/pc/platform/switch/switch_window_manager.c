#ifdef __SWITCH__

#include <SDL2/SDL.h>
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>

#include "pc/gfx/gfx_window_manager.h"
#include "pc/gfx/gfx_window_opengl.h"
#include "pc/gfx/gfx_dummy.h"
#include "pc/gfx/gfx_screen_config.h"
#include "pc/configfile.h"
#include "pc/cliopts.h"
#include "pc/controller/controller_bind_mapping.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_inputbox.h"
#include "pc/platform/switch/switch_platform.h"
#include "pc/pc_main.h"

static struct GfxWindowBackendAPI *sBackends[GFX_WINDOW_BACKEND_COUNT] = {
    [GFX_WINDOW_BACKEND_OPENGL] = &gfx_window_opengl,
    [GFX_WINDOW_BACKEND_DUMMY] = &gfx_window_dummy,
};

static enum GfxWindowBackend sBackend = GFX_WINDOW_BACKEND_DUMMY;
static SDL_Window *sWindow = NULL;
static SwitchPlatformState sPlatformState;
static bool sPlatformReady = false;
static bool sNativeKeyboardOpen = false;

static kb_callback_t sKeyDown = NULL;
static kb_callback_t sKeyUp = NULL;
static void (*sAllKeysUp)(void) = NULL;
static void (*sTextInput)(char *) = NULL;
static void (*sTextEditing)(char *, int) = NULL;
static void (*sScroll)(float, float) = NULL;

void gfx_wm_set_window(SDL_Window *window) {
    sWindow = window;
}

SDL_Window *gfx_wm_get_window(void) {
    return sWindow;
}

static void switch_handle_key(bool down, SDL_Scancode scancode) {
    int code = translate_sdl_scancode((int)scancode);
    if (down) {
        if (sKeyDown) sKeyDown(code);
    } else {
        if (sKeyUp) sKeyUp(code);
    }
}

static void switch_show_native_keyboard(void) {
    if (sNativeKeyboardOpen || gInteractableFocus == NULL ||
        gInteractableFocus->interactable == NULL ||
        gInteractableFocus->interactable->on_text_input != djui_inputbox_on_text_input) {
        return;
    }

    struct DjuiInputbox *inputbox = (struct DjuiInputbox *)gInteractableFocus;
    if (inputbox->buffer == NULL || inputbox->bufferSize <= 1) {
        return;
    }

    SwkbdConfig keyboard;
    Result rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        fprintf(stderr, "swkbdCreate failed: 0x%08x\n", (unsigned int)rc);
        return;
    }

    sNativeKeyboardOpen = true;

    if (inputbox->passwordChar[0] != '\0') {
        swkbdConfigMakePresetPassword(&keyboard);
    } else {
        swkbdConfigMakePresetDefault(&keyboard);
    }
    swkbdConfigSetInitialText(&keyboard, inputbox->buffer);
    swkbdConfigSetStringLenMax(&keyboard, (u32)inputbox->bufferSize - 1u);

    /*
     * stringLenMax is measured in characters while DjuiInputbox::bufferSize is
     * measured in UTF-8 bytes. Give swkbd enough room for the worst-case UTF-8
     * representation, then let the existing DJUI text-input callback perform
     * its normal sanitizing/truncation and Unicode end cleanup.
     */
    size_t outputSize = ((size_t)inputbox->bufferSize * 4u) + 1u;
    char *output = (char *)calloc(outputSize, 1);
    if (output != NULL) {
        struct DjuiBase *focus = gInteractableFocus;
        rc = swkbdShow(&keyboard, output, outputSize);
        if (R_SUCCEEDED(rc) && gInteractableFocus == focus && sTextInput != NULL) {
            djui_inputbox_select_all(inputbox);
            sTextInput(output);
        }
        free(output);
    }

    swkbdClose(&keyboard);
    sNativeKeyboardOpen = false;
}

void gfx_wm_init(const char *window_title) {
    if (gCLIOpts.headless) return;

    if (!sPlatformReady) {
        sPlatformReady = switch_platform_init(&sPlatformState);
        if (sPlatformReady) {
            switch_platform_set_exit_callback(game_exit);
        }
    }

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
        game_exit();
    }

    sBackend = configGraphicsBackend;
    if (sBackend != GFX_WINDOW_BACKEND_OPENGL && sBackend != GFX_WINDOW_BACKEND_DUMMY) {
        sBackend = GFX_WINDOW_BACKEND_OPENGL;
    }

    sBackends[sBackend]->init(window_title);
    controller_bind_init();
}

void gfx_wm_main_loop(void (*run_one_game_iter)(void)) {
    if (sPlatformReady && !switch_platform_main_loop(&sPlatformState)) {
        game_exit();
        return;
    }
    run_one_game_iter();
}

void gfx_wm_get_dimensions(uint32_t *width, uint32_t *height) {
    if (sBackend == GFX_WINDOW_BACKEND_DUMMY || sWindow == NULL) {
        if (width) *width = 1280;
        if (height) *height = 720;
        return;
    }

    int w = 1280;
    int h = 720;
    SDL_GetWindowSize(sWindow, &w, &h);
    if (width) *width = (uint32_t)w;
    if (height) *height = (uint32_t)h;
}

void gfx_wm_handle_events(void) {
    if (sBackend == GFX_WINDOW_BACKEND_DUMMY) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                game_exit();
                return;
            case SDL_TEXTINPUT:
                if (sTextInput) sTextInput(event.text.text);
                break;
            case SDL_TEXTEDITING:
                if (sTextEditing) sTextEditing(event.edit.text, event.edit.start);
                break;
            case SDL_KEYDOWN:
                switch_handle_key(true, event.key.keysym.scancode);
                break;
            case SDL_KEYUP:
                switch_handle_key(false, event.key.keysym.scancode);
                break;
            case SDL_MOUSEWHEEL:
                if (sScroll) sScroll((float)event.wheel.x, (float)event.wheel.y);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && sAllKeysUp) {
                    sAllKeysUp();
                }
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    configWindow.w = event.window.data1;
                    configWindow.h = event.window.data2;
                }
                break;
            default:
                break;
        }
        sBackends[sBackend]->handle_events(event);
    }

    /* Horizon owns the display mode. Do not recreate/resize the GL context. */
    configWindow.fullscreen = true;
    configWindow.settings_changed = false;
    configWindow.reset = false;
}

void gfx_wm_set_keyboard_callbacks(kb_callback_t on_key_down, kb_callback_t on_key_up,
    void (*on_all_keys_up)(void), void (*on_text_input)(char *), void (*on_text_editing)(char *, int)) {
    sKeyDown = on_key_down;
    sKeyUp = on_key_up;
    sAllKeysUp = on_all_keys_up;
    sTextInput = on_text_input;
    sTextEditing = on_text_editing;
}

void gfx_wm_set_scroll_callback(void (*on_scroll)(float, float)) {
    sScroll = on_scroll;
}

bool gfx_wm_start_frame(void) {
    return sBackends[sBackend]->start_frame();
}

void gfx_wm_swap_buffers_begin(void) {
    sBackends[sBackend]->swap_buffers_begin();
}

void gfx_wm_swap_buffers_end(void) {
    sBackends[sBackend]->swap_buffers_end();
}

double gfx_wm_get_time(void) {
    return sBackends[sBackend]->get_time();
}

void gfx_wm_delay(u32 ms) {
    SDL_Delay(ms);
}

int gfx_wm_get_max_msaa(void) {
    return sBackends[sBackend]->get_max_msaa();
}

void gfx_wm_set_window_title(const char *title) {
    if (sWindow) SDL_SetWindowTitle(sWindow, title);
}

void gfx_wm_reset_window_title(void) {
    if (sWindow) SDL_SetWindowTitle(sWindow, TITLE);
}

bool gfx_wm_has_focus(void) {
    return !sPlatformReady || !sPlatformState.suspended;
}

void gfx_wm_start_text_input(void) {
    SDL_StartTextInput();
    switch_show_native_keyboard();
}

void gfx_wm_stop_text_input(void) {
    SDL_StopTextInput();
}

char *gfx_wm_get_clipboard_text(void) {
    static char buffer[WAPI_CLIPBOARD_BUFSIZ];
    buffer[0] = '\0';
    char *text = SDL_GetClipboardText();
    if (text) {
        snprintf(buffer, sizeof(buffer), "%s", text);
        SDL_free(text);
    }
    return buffer;
}

void gfx_wm_set_clipboard_text(const char *text) {
    SDL_SetClipboardText(text ? text : "");
}

void gfx_wm_set_cursor_visible(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

void gfx_wm_shutdown(void) {
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        if (context) SDL_GL_DeleteContext(context);
        if (sWindow) {
            SDL_DestroyWindow(sWindow);
            sWindow = NULL;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    if (sPlatformReady) {
        switch_platform_set_exit_callback(NULL);
        switch_platform_shutdown(&sPlatformState);
        sPlatformReady = false;
    }
}

#endif
