#ifdef __3DS__

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pc/gfx/gfx_window_manager.h"
#include "pc/configfile.h"
#include "pc/pc_main.h"
#include "pc/controller/controller_keyboard.h"
#include "pc/djui/djui_inputbox.h"
#include "pc/djui/djui_interactable.h"
#include "pc/platform/new3ds/new3ds_runtime.h"
#include "pc/platform/new3ds/new3ds_platform_ui.h"
#include "pc/platform/new3ds/new3ds_boot_progress.h"
#ifdef NEW3DS_ENABLE_NATIVE_KEYBOARD
#include "pc/platform/new3ds/new3ds_bottom_ui.h"
#include "pc/platform/new3ds/new3ds_log.h"
#endif

/*
 * Native swkbd uses the bottom screen as a system applet. That conflicts with
 * PrintConsole (and Citro3D ownership) and has crashed CoopNet/chat text entry
 * on hardware and Azahar. Match Switch: CoopNet uses in-game DJUI keypads.
 * Define NEW3DS_ENABLE_NATIVE_KEYBOARD only for experimental builds.
 */

static New3dsRuntimeState sRuntime;
static bool sRuntimeReady = false;
static bool sGraphicsReady = false;
static bool sForceExitOnStart = false;
static bool sTextInputActive = false;
static bool sNativeKeyboardPending = false;
static struct DjuiInputbox *sNativeKeyboardTarget = NULL;
static kb_callback_t sKeyDown = NULL;
static kb_callback_t sKeyUp = NULL;
static void (*sAllKeysUp)(void) = NULL;
static void (*sTextInput)(char *) = NULL;
static void (*sTextEditing)(char *, int) = NULL;
static void (*sScroll)(float, float) = NULL;
static char sClipboard[WAPI_CLIPBOARD_BUFSIZ];

#ifdef NEW3DS_ENABLE_NATIVE_KEYBOARD
static void new3ds_show_native_keyboard(void);
#endif

void gfx_wm_set_window(SDL_Window *window) {
    (void)window;
}

SDL_Window *gfx_wm_get_window(void) {
    return NULL;
}

void gfx_wm_init(const char *window_title) {
    (void)window_title;

    if (!sGraphicsReady) {
        if (!new3ds_boot_progress_gfx_started()) {
            gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);
            gfxSet3D(false);
        }
        sGraphicsReady = true;
        new3ds_boot_progress_set("Graphics runtime...");
    }

    if (!sRuntimeReady) {
        sRuntimeReady = new3ds_runtime_init(&sRuntime);
        if (!sRuntimeReady) {
            new3ds_platform_show_exit_message("Runtime initialization failed.");
            gfx_wm_shutdown();
            new3ds_platform_quit();
        }
        if (!sRuntime.is_new_3ds) {
            new3ds_platform_show_exit_message(new3ds_platform_unsupported_hardware_message());
            gfx_wm_shutdown();
            new3ds_platform_quit();
        }
    }

    configWindow.fullscreen = true;
    configWindow.w = 400;
    configWindow.h = 240;
    configWindow.settings_changed = false;
    configWindow.reset = false;
    sClipboard[0] = '\0';
}

void gfx_wm_set_force_exit_on_start(bool enable) {
    sForceExitOnStart = enable;
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

    if (sForceExitOnStart && (sRuntime.input.down & KEY_START) != 0) {
        game_exit();
        return;
    }

#ifdef NEW3DS_ENABLE_NATIVE_KEYBOARD
    /* Deferred: open swkbd outside of Djui focus begin, between frames. */
    if (sNativeKeyboardPending && !sTextInputActive) {
        new3ds_show_native_keyboard();
    }
#endif

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
    return sRuntimeReady && !sRuntime.exit_requested && !sTextInputActive;
}

#ifdef NEW3DS_ENABLE_NATIVE_KEYBOARD
static void new3ds_send_virtual_key(int scancode) {
    if (sKeyDown != NULL) {
        sKeyDown(scancode);
    }
    if (sKeyUp != NULL) {
        sKeyUp(scancode);
    }
}

static void new3ds_show_native_keyboard(void) {
    struct DjuiInputbox *inputbox = sNativeKeyboardTarget;
    sNativeKeyboardPending = false;
    sNativeKeyboardTarget = NULL;

    if (sTextInputActive || inputbox == NULL ||
        gInteractableFocus != &inputbox->base ||
        inputbox->base.interactable == NULL ||
        inputbox->base.interactable->on_text_input != djui_inputbox_on_text_input) {
        return;
    }
    if (inputbox->buffer == NULL || inputbox->bufferSize <= 1) {
        return;
    }

    enum { NEW3DS_SWKBD_BUFFER_SIZE = 512 };
    char text[NEW3DS_SWKBD_BUFFER_SIZE];
    strncpy(text, inputbox->buffer, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    int max_text_length = (int)inputbox->bufferSize - 1;
    if (max_text_length > (int)sizeof(text) - 1) {
        max_text_length = (int)sizeof(text) - 1;
    }
    if (max_text_length < 1) {
        return;
    }

    const bool password = inputbox->passwordChar[0] != '\0';

    /* Pause PrintConsole so the swkbd applet can own the bottom screen. */
    new3ds_bottom_ui_set_paused(true);
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "keyboard", "native swkbd begin");

    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, max_text_length);
    swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
    /* Avoid DARKEN_TOP_SCREEN — it fights Citro3D top ownership. */
    swkbdSetFeatures(&keyboard, SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
    swkbdSetHintText(&keyboard, password ? "Enter password" : "Enter text");
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetInitialText(&keyboard, text);
    if (password) {
        swkbdSetPasswordMode(&keyboard, SWKBD_PASSWORD_HIDE_DELAY);
    }

    sTextInputActive = true;
    const SwkbdButton button = swkbdInputText(&keyboard, text, sizeof(text));
    sTextInputActive = false;

    /* Restore bottom log console after the applet returns. */
    new3ds_bottom_ui_reinit_console();
    new3ds_bottom_ui_set_paused(false);
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "keyboard", "native swkbd end button=%d", (int)button);

    if (button == SWKBD_BUTTON_RIGHT && gInteractableFocus == &inputbox->base) {
        djui_inputbox_select_all(inputbox);
        djui_inputbox_on_text_input(&inputbox->base, text);
        new3ds_send_virtual_key(SCANCODE_ENTER);
    } else {
        new3ds_send_virtual_key(SCANCODE_ESCAPE);
    }
}
#endif /* NEW3DS_ENABLE_NATIVE_KEYBOARD */

void gfx_wm_start_text_input(void) {
#ifdef NEW3DS_ENABLE_NATIVE_KEYBOARD
    if (gInteractableFocus != NULL &&
        gInteractableFocus->interactable != NULL &&
        gInteractableFocus->interactable->on_text_input == djui_inputbox_on_text_input) {
        sNativeKeyboardTarget = (struct DjuiInputbox *)gInteractableFocus;
        sNativeKeyboardPending = true;
    }
#else
    /*
     * Intentionally a no-op: native swkbd crashes with PrintConsole on bottom.
     * Host/Join/CoopNet password + IP use djui_panel_switch_text_entry / private keypad.
     */
    (void)sNativeKeyboardPending;
    (void)sNativeKeyboardTarget;
#endif
}

void gfx_wm_stop_text_input(void) {
    if (!sTextInputActive) {
        sNativeKeyboardPending = false;
        sNativeKeyboardTarget = NULL;
    }
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
    sTextInputActive = false;
    sNativeKeyboardPending = false;
    sNativeKeyboardTarget = NULL;

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
