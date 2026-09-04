#ifdef __3DS__

#include "controller_new3ds.h"

#include <3ds.h>
#include <math.h>
#include <string.h>

#include "macros.h"
#include "pc/configfile.h"
#include "pc/platform/new3ds/new3ds_runtime.h"

static float new3ds_clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void new3ds_transform_stick(float *x, float *y, bool right_stick) {
    const bool rotate = right_stick ? configStick.rotateRight : configStick.rotateLeft;
    const bool invert_x = right_stick ? configStick.invertRightX : configStick.invertLeftX;
    const bool invert_y = right_stick ? configStick.invertRightY : configStick.invertLeftY;

    if (rotate) {
        const float old_x = *x;
        *x = -*y;
        *y = old_x;
    }
    if (invert_x) *x = -*x;
    if (invert_y) *y = -*y;
}

static void new3ds_scale_stick(s8 *out_x, s8 *out_y, const circlePosition *stick, bool right_stick) {
    /* Circle Pad ~±156; New 3DS C-Stick typically reaches ~±167. */
    const float axis_max = right_stick ? 167.0f : 156.0f;
    float x = new3ds_clampf((float)stick->dx / axis_max, -1.0f, 1.0f);
    float y = new3ds_clampf((float)stick->dy / axis_max, -1.0f, 1.0f);

    new3ds_transform_stick(&x, &y, right_stick);

    const float magnitude = sqrtf(x * x + y * y);
    /* Slightly lower deadzone on the C-Stick so camera responds sooner. */
    float deadzone = ((float)configStickDeadzone * (float)DEADZONE_STEP) / 32768.0f;
    if (right_stick) {
        deadzone *= 0.65f;
    }
    deadzone = new3ds_clampf(deadzone, 0.0f, 0.95f);

    if (magnitude <= deadzone || magnitude <= 0.0001f) {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    /* Remove the configured deadzone while preserving the stick direction. */
    float remapped = (magnitude - deadzone) / (1.0f - deadzone);
    remapped = new3ds_clampf(remapped, 0.0f, 1.0f);
    const float scale = remapped / magnitude;
    x = new3ds_clampf(x * scale, -1.0f, 1.0f);
    y = new3ds_clampf(y * scale, -1.0f, 1.0f);

    int scaled_x = (int)lrintf(x * 127.0f);
    int scaled_y = (int)lrintf(y * 127.0f);
    if (scaled_x < -128) scaled_x = -128;
    if (scaled_x > 127) scaled_x = 127;
    if (scaled_y < -128) scaled_y = -128;
    if (scaled_y > 127) scaled_y = 127;

    *out_x = (s8)scaled_x;
    *out_y = (s8)scaled_y;
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

static void new3ds_apply_cstick_buttons(OSContPad *pad) {
    /* Digital C-button fallback for vanilla camera when analog free-cam is off. */
    const s8 threshold = 35;
    if (pad->ext_stick_x < -threshold) pad->button |= L_CBUTTONS;
    if (pad->ext_stick_x > threshold) pad->button |= R_CBUTTONS;
    if (pad->ext_stick_y > threshold) pad->button |= U_CBUTTONS;
    if (pad->ext_stick_y < -threshold) pad->button |= D_CBUTTONS;
}

static void controller_new3ds_init(void) {
    /* The New 3DS runtime owns HID initialization/polling through libctru. */
}

static void controller_new3ds_read(OSContPad *pad) {
    if (pad == NULL) return;

    New3dsRuntimeState *runtime = new3ds_runtime_active();
    if (runtime == NULL || !runtime->initialized) return;

    new3ds_apply_buttons(pad, runtime->input.held);
    new3ds_scale_stick(&pad->stick_x, &pad->stick_y, &runtime->input.circle, false);
    new3ds_scale_stick(&pad->ext_stick_x, &pad->ext_stick_y, &runtime->input.cstick, true);
    new3ds_apply_cstick_buttons(pad);
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
