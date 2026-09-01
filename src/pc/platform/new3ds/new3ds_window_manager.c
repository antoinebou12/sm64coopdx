#ifdef __3DS__

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pc/gfx/gfx_window_manager.h"
#include "pc/configfile.h"
#include "pc/pc_main.h"
#include "pc/platform/new3ds/new3ds_runtime.h"

static New3dsRuntimeState sRuntime;
static bool sRuntimeReady = false;
static bool sGraphicsReady = false;
static kb_callback_t sKeyDown = NULL;
static kb_callback_t sKeyUp = NULL;
static void (*sAllKeysUp)(void) = NULL;
static void (*sTextInput)(char *) = NULL;
static void (*sTextEditing)(char *, int) = NULL;
static void (*sScroll)(float, float) = NULL;
static char sClipboard[WAPI_CLIPBOARD_BUFSIZ];

void gfx_wm_set_window(SDL_Window *window) {
    (void)window;
}

SDL_Window *gfx_wm_get_window(void) {
    return NULL;
}

void gfx_wm_init(const char *window_title) {
    (void)window_title;

    if (!sGraphicsReady) {
        gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
        gfxSet3D(false);
        sGraphicsReady = true;
    }

    if (!sRuntimeReady) {
        sRuntimeReady = new3ds_runtime_init(&sRuntime);
        if (!sRuntimeReady || !sRuntime.is_new_3ds) {
            game_exit();
            return;
        }
    }

    configWindow.fullscreen = true;
    configWindow.w = 400;
    configWindow.h = 240;
    configWindow.settings_changed = false;
    configWindow.reset = false;
    sClipboard[0] = '\0';
}

void gfx_wm_main_loop(void (*run_one_game_iter)(void)) {
    if (!sRuntimeReady || run_one_game_iter == NULL) {
        game_exit();
        return;
    }

    if (!new3ds_runtime_poll(&sRuntime)) {
        game_exit();
        return;
    }

    run_one_game_iter();
}

void gfx_wm_get_dimensions(uint32_t *width, uint32_t *height) {
    if (width != NULL) *width = 400;
    if (height != NULL) *height = 240;
}

void gfx_wm_handle_events(void) {
    /*
     * HID is sampled once per game iteration by new3ds_runtime_poll().
     * Controller input consumes that shared snapshot, so do not scan HID a
     * second time here or edge-triggered button state can be lost.
     */
    configWindow.fullscreen = true;
    configWindow.settings_changed = false;
    configWindow.reset = false;
}

void gfx_wm_set_keyboard_callbacks(
    kb_callback_t on_key_down,
    kb_callback_t on_key_up,
    void (*on_all_keys_up)(void),
    void (*on_text_input)(char *),
    void (*on_text_editing)(char *, int)) {
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
    /* Citro3D frame ownership belongs to the New 3DS rendering backend. */
    return sRuntimeReady && !sRuntime.exit_requested;
}

void gfx_wm_swap_buffers_begin(void) {
    /* Citro3D rendering backend submits/presents the frame. */
}

void gfx_wm_swap_buffers_end(void) {
}

double gfx_wm_get_time(void) {
    return new3ds_runtime_time_seconds();
}

void gfx_wm_delay(u32 ms) {
    new3ds_runtime_sleep_ms(ms);
}

int gfx_wm_get_max_msaa(void) {
    return 0;
}

void gfx_wm_set_window_title(const char *title) {
    (void)title;
}

void gfx_wm_reset_window_title(void) {
}

bool gfx_wm_has_focus(void) {
    return sRuntimeReady && !sRuntime.exit_requested;
}

void gfx_wm_start_text_input(void) {
    /* Native software-keyboard integration is implemented as a later UX step. */
}

void gfx_wm_stop_text_input(void) {
}

char *gfx_wm_get_clipboard_text(void) {
    return sClipboard;
}

void gfx_wm_set_clipboard_text(const char *text) {
    if (text == NULL) {
        sClipboard[0] = '\0';
        return;
    }

    strncpy(sClipboard, text, sizeof(sClipboard) - 1);
    sClipboard[sizeof(sClipboard) - 1] = '\0';
}

void gfx_wm_set_cursor_visible(bool visible) {
    (void)visible;
}

void gfx_wm_shutdown(void) {
    if (sAllKeysUp != NULL) {
        sAllKeysUp();
    }

    sKeyDown = NULL;
    sKeyUp = NULL;
    sAllKeysUp = NULL;
    sTextInput = NULL;
    sTextEditing = NULL;
    sScroll = NULL;

    if (sRuntimeReady) {
        new3ds_runtime_shutdown(&sRuntime);
        sRuntimeReady = false;
    }

    if (sGraphicsReady) {
        gfxExit();
        sGraphicsReady = false;
    }
}

#endif /* __3DS__ */
