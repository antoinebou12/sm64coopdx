#ifdef __3DS__

#include "controller_new3ds.h"

#include <3ds.h>
#include <string.h>

#include "macros.h"
#include "pc/platform/new3ds/new3ds_runtime.h"

static s8 new3ds_scale_axis(s16 value) {
    float normalized = new3ds_runtime_axis_normalized(value);
    int scaled = (int)(normalized * 127.0f);
    if (scaled < -128) scaled = -128;
    if (scaled > 127) scaled = 127;
    return (s8)scaled;
}

static void new3ds_apply_buttons(OSContPad *pad, u32 keys) {
    if (keys & KEY_A) pad->button |= A_BUTTON;
    if (keys & KEY_B) pad->button |= B_BUTTON;
    if (keys & KEY_X) pad->button |= X_BUTTON;
    if (keys & KEY_Y) pad->button |= Y_BUTTON;

    if (keys & KEY_L) pad->button |= L_TRIG;
    if (keys & KEY_R) pad->button |= R_TRIG;
    if (keys & KEY_ZL) pad->button |= Z_TRIG;
    if (keys & KEY_ZR) pad->button |= Z_TRIG;

    if (keys & KEY_START) pad->button |= START_BUTTON;
    if (keys & KEY_DUP) pad->button |= U_JPAD;
    if (keys & KEY_DDOWN) pad->button |= D_JPAD;
    if (keys & KEY_DLEFT) pad->button |= L_JPAD;
    if (keys & KEY_DRIGHT) pad->button |= R_JPAD;
}

static void new3ds_apply_cstick_buttons(OSContPad *pad, const circlePosition *stick) {
    const s16 threshold = 45;
    if (stick->dx < -threshold) pad->button |= L_CBUTTONS;
    if (stick->dx > threshold) pad->button |= R_CBUTTONS;
    if (stick->dy > threshold) pad->button |= U_CBUTTONS;
    if (stick->dy < -threshold) pad->button |= D_CBUTTONS;
}

static void controller_new3ds_init(void) {
    /* The New 3DS runtime owns HID initialization/polling through libctru. */
}

static void controller_new3ds_read(OSContPad *pad) {
    if (pad == NULL) return;

    New3dsRuntimeState *runtime = new3ds_runtime_active();
    if (runtime == NULL || !runtime->initialized) return;

    new3ds_apply_buttons(pad, runtime->input.held);
    new3ds_apply_cstick_buttons(pad, &runtime->input.cstick);

    pad->stick_x = new3ds_scale_axis(runtime->input.circle.dx);
    pad->stick_y = new3ds_scale_axis(runtime->input.circle.dy);
    pad->ext_stick_x = new3ds_scale_axis(runtime->input.cstick.dx);
    pad->ext_stick_y = new3ds_scale_axis(runtime->input.cstick.dy);
}

static u32 controller_new3ds_rawkey(void) {
    return VK_INVALID;
}

static void controller_new3ds_shutdown(void) {
}

struct ControllerAPI controller_new3ds = {
    VK_INVALID,
    controller_new3ds_init,
    controller_new3ds_read,
    controller_new3ds_rawkey,
    NULL,
    NULL,
    NULL,
    controller_new3ds_shutdown,
};

#endif /* __3DS__ */
