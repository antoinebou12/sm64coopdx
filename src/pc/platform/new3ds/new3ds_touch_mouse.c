#ifdef __3DS__

/*
 * Map the bottom touch screen onto the top-screen DJUI cursor.
 * Physical touch only exists on the bottom (320x240); scale X to 400.
 * When the companion bottom UI claims a tap, suppress the click.
 */

#include "pc/controller/controller_mouse.h"
#include "pc/platform/new3ds/new3ds_runtime.h"
#include "pc/platform/new3ds/new3ds_bottom_ui.h"

bool mouse_init_ok = true;
u32 mouse_buttons = 0;
f32 mouse_x = 0;
f32 mouse_y = 0;
u32 mouse_window_buttons = 0;
u32 mouse_window_buttons_pressed = 0;
u32 mouse_window_buttons_released = 0;
f32 mouse_window_x = 0;
f32 mouse_window_y = 0;
u32 mouse_scroll_timestamp = 0;
f32 mouse_scroll_x = 0;
f32 mouse_scroll_y = 0;
bool mouse_relative_enabled = false;

static bool sPrevTouching = false;

void controller_mouse_read_window(void) {
    if (new3ds_bottom_ui_ready()) {
        new3ds_bottom_ui_poll_input();
    }

    New3dsRuntimeState *runtime = new3ds_runtime_active();
    mouse_window_buttons_pressed = 0;
    mouse_window_buttons_released = 0;

    if (runtime == NULL) {
        mouse_window_buttons = 0;
        sPrevTouching = false;
        return;
    }

    const bool touching = runtime->input.touching;
    const bool bottomClaims = new3ds_bottom_ui_touch_consumed();

    if (touching) {
        /* Bottom 320x240 -> top logical 400x240 for DJUI cursor. */
        mouse_window_x = ((f32)runtime->input.touch.px) * (400.0f / 320.0f);
        mouse_window_y = (f32)runtime->input.touch.py;
        if (mouse_window_x < 0.0f) mouse_window_x = 0.0f;
        if (mouse_window_x > 399.0f) mouse_window_x = 399.0f;
        if (mouse_window_y < 0.0f) mouse_window_y = 0.0f;
        if (mouse_window_y > 239.0f) mouse_window_y = 239.0f;
    }

    if (bottomClaims) {
        mouse_window_buttons = 0;
        sPrevTouching = touching;
        return;
    }

    if (touching) {
        mouse_window_buttons = L_MOUSE_BUTTON;
        if (!sPrevTouching) {
            mouse_window_buttons_pressed = L_MOUSE_BUTTON;
        }
    } else {
        mouse_window_buttons = 0;
        if (sPrevTouching) {
            mouse_window_buttons_released = L_MOUSE_BUTTON;
        }
    }

    mouse_buttons = mouse_window_buttons;
    sPrevTouching = touching;
}

void controller_mouse_read_relative(void) {
    /* Relative mouse is unused on New 3DS. */
}

void controller_mouse_enter_relative(void) {
    mouse_relative_enabled = true;
}

void controller_mouse_leave_relative(void) {
    mouse_relative_enabled = false;
}

void mouse_on_scroll(float x, float y) {
    (void)x;
    (void)y;
}

#endif /* __3DS__ */
