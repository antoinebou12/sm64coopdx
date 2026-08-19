#ifdef __SWITCH__

#include "controller_switch.h"

#include <math.h>
#include <string.h>

#include "macros.h"

#include "pc/platform/switch/switch_input.h"

static bool sInitialized = false;

static s8 scale_axis(int16_t axis) {
    int32_t value = axis / 256;
    if (value < -128) value = -128;
    if (value > 127) value = 127;
    return (s8)value;
}

static void apply_buttons(OSContPad *pad, uint32_t buttons) {
    if (buttons & SWITCH_INPUT_A) pad->button |= A_BUTTON;
    if (buttons & SWITCH_INPUT_B) pad->button |= B_BUTTON;
    if (buttons & SWITCH_INPUT_X) pad->button |= X_BUTTON;
    if (buttons & SWITCH_INPUT_Y) pad->button |= Y_BUTTON;

    if (buttons & SWITCH_INPUT_L) pad->button |= L_TRIG;
    if (buttons & SWITCH_INPUT_R) pad->button |= R_TRIG;
    if (buttons & SWITCH_INPUT_ZL) pad->button |= R_TRIG;
    if (buttons & SWITCH_INPUT_ZR) pad->button |= Z_TRIG;

    if (buttons & SWITCH_INPUT_PLUS) pad->button |= START_BUTTON;
    if (buttons & SWITCH_INPUT_UP) pad->button |= U_JPAD;
    if (buttons & SWITCH_INPUT_DOWN) pad->button |= D_JPAD;
    if (buttons & SWITCH_INPUT_LEFT) pad->button |= L_JPAD;
    if (buttons & SWITCH_INPUT_RIGHT) pad->button |= R_JPAD;
}

static void apply_right_stick_c_buttons(OSContPad *pad, int16_t x, int16_t y) {
    const int16_t threshold = 0x4000;
    if (x < -threshold) pad->button |= L_CBUTTONS;
    if (x > threshold) pad->button |= R_CBUTTONS;
    if (y > threshold) pad->button |= U_CBUTTONS;
    if (y < -threshold) pad->button |= D_CBUTTONS;
}

static bool init_switch_controller(void) {
    if (sInitialized) return true;
    sInitialized = switch_input_init();
    return sInitialized;
}

void controller_switch_poll(void) {
    if (!init_switch_controller()) return;
    switch_input_poll();
}

bool controller_switch_read_slot(u8 slot, OSContPad *pad) {
    if (pad == NULL || slot >= SWITCH_INPUT_MAX_PLAYERS) return false;
    memset(pad, 0, sizeof(*pad));

    SwitchInputPad input;
    if (!switch_input_get(slot, &input)) return false;

    apply_buttons(pad, input.buttons);
    apply_right_stick_c_buttons(pad, input.right_x, input.right_y);

    pad->stick_x = scale_axis(input.left_x);
    pad->stick_y = scale_axis(input.left_y);
    pad->ext_stick_x = scale_axis(input.right_x);
    pad->ext_stick_y = scale_axis(input.right_y);
    return true;
}

u8 controller_switch_connected_count(void) {
    if (!init_switch_controller()) return 0;
    return switch_input_connected_count();
}

bool controller_switch_rumble_slot(u8 slot, f32 strength) {
    if (!init_switch_controller()) return false;
    return switch_input_rumble(slot, strength);
}

void controller_switch_rumble_stop_slot(u8 slot) {
    if (!sInitialized) return;
    switch_input_rumble_stop(slot);
}

static void controller_switch_init(void) {
    init_switch_controller();
}

static void controller_switch_read(OSContPad *pad) {
    controller_switch_poll();
    controller_switch_read_slot(0, pad);
}

static u32 controller_switch_rawkey(void) {
    return VK_INVALID;
}

static void controller_switch_rumble_play(f32 strength, UNUSED f32 length) {
    controller_switch_rumble_slot(0, strength);
}

static void controller_switch_rumble_stop(void) {
    controller_switch_rumble_stop_slot(0);
}

static void controller_switch_shutdown(void) {
    if (!sInitialized) return;
    switch_input_shutdown();
    sInitialized = false;
}

struct ControllerAPI controller_switch = {
    VK_INVALID,
    controller_switch_init,
    controller_switch_read,
    controller_switch_rawkey,
    controller_switch_rumble_play,
    controller_switch_rumble_stop,
    NULL,
    controller_switch_shutdown,
};

#endif /* __SWITCH__ */
