#ifdef __SWITCH__

#include "switch_input.h"

#include <switch.h>
#include <string.h>

static PadState sPads[SWITCH_INPUT_MAX_PLAYERS];
static SwitchInputPad sStates[SWITCH_INPUT_MAX_PLAYERS];
static bool sInitialized = false;
static HidVibrationDeviceHandle sVibrationHandles[SWITCH_INPUT_MAX_PLAYERS][2];
static uint8_t sVibrationHandleCount[SWITCH_INPUT_MAX_PLAYERS];
static uint32_t sVibrationStyle[SWITCH_INPUT_MAX_PLAYERS];

static const HidNpadIdType sNpadIds[SWITCH_INPUT_MAX_PLAYERS] = {
    HidNpadIdType_No1,
    HidNpadIdType_No2,
    HidNpadIdType_No3,
    HidNpadIdType_No4,
};

static int16_t clamp_axis(int32_t value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return (int16_t)value;
}

static SwitchInputStyle get_style(const PadState *pad) {
    const uint32_t style = padGetStyleSet(pad);
    if (padIsHandheld(pad) || (style & HidNpadStyleTag_NpadHandheld)) return SWITCH_INPUT_STYLE_HANDHELD;
    if (style & HidNpadStyleTag_NpadFullKey) return SWITCH_INPUT_STYLE_PRO;
    if (style & HidNpadStyleTag_NpadJoyDual) return SWITCH_INPUT_STYLE_JOY_DUAL;
    if (style & HidNpadStyleTag_NpadJoyLeft) return SWITCH_INPUT_STYLE_JOY_LEFT;
    if (style & HidNpadStyleTag_NpadJoyRight) return SWITCH_INPUT_STYLE_JOY_RIGHT;
    if (style & HidNpadStyleTag_NpadGc) return SWITCH_INPUT_STYLE_GAMECUBE;
    if (style & HidNpadStyleTag_NpadLagon) return SWITCH_INPUT_STYLE_N64;
    return SWITCH_INPUT_STYLE_OTHER;
}

static uint32_t map_full_buttons(uint64_t raw) {
    uint32_t out = 0;
    if (raw & HidNpadButton_A) out |= SWITCH_INPUT_A;
    if (raw & HidNpadButton_B) out |= SWITCH_INPUT_B;
    if (raw & HidNpadButton_X) out |= SWITCH_INPUT_X;
    if (raw & HidNpadButton_Y) out |= SWITCH_INPUT_Y;
    if (raw & HidNpadButton_L) out |= SWITCH_INPUT_L;
    if (raw & HidNpadButton_R) out |= SWITCH_INPUT_R;
    if (raw & HidNpadButton_ZL) out |= SWITCH_INPUT_ZL;
    if (raw & HidNpadButton_ZR) out |= SWITCH_INPUT_ZR;
    if (raw & HidNpadButton_Plus) out |= SWITCH_INPUT_PLUS;
    if (raw & HidNpadButton_Minus) out |= SWITCH_INPUT_MINUS;
    if (raw & HidNpadButton_Up) out |= SWITCH_INPUT_UP;
    if (raw & HidNpadButton_Down) out |= SWITCH_INPUT_DOWN;
    if (raw & HidNpadButton_Left) out |= SWITCH_INPUT_LEFT;
    if (raw & HidNpadButton_Right) out |= SWITCH_INPUT_RIGHT;
    if (raw & HidNpadButton_StickL) out |= SWITCH_INPUT_STICK_L;
    if (raw & HidNpadButton_StickR) out |= SWITCH_INPUT_STICK_R;
    return out;
}

static uint32_t map_single_left_buttons(uint64_t raw) {
    uint32_t out = 0;

    /*
     * A single left Joy-Con is held horizontally. Map its four D-pad buttons
     * onto the four face-button positions and expose SL/SR as shoulders.
     * This keeps the logical layout consistent for local multiplayer without
     * requiring a separate controller-binding profile.
     */
    if (raw & HidNpadButton_Down) out |= SWITCH_INPUT_A;
    if (raw & HidNpadButton_Left) out |= SWITCH_INPUT_B;
    if (raw & HidNpadButton_Up) out |= SWITCH_INPUT_X;
    if (raw & HidNpadButton_Right) out |= SWITCH_INPUT_Y;
    if (raw & HidNpadButton_LeftSL) out |= SWITCH_INPUT_L;
    if (raw & HidNpadButton_LeftSR) out |= SWITCH_INPUT_R;
    if (raw & HidNpadButton_Minus) out |= SWITCH_INPUT_PLUS;
    if (raw & HidNpadButton_StickL) out |= SWITCH_INPUT_STICK_L;
    return out;
}

static uint32_t map_single_right_buttons(uint64_t raw) {
    uint32_t out = 0;
    if (raw & HidNpadButton_A) out |= SWITCH_INPUT_A;
    if (raw & HidNpadButton_B) out |= SWITCH_INPUT_B;
    if (raw & HidNpadButton_X) out |= SWITCH_INPUT_X;
    if (raw & HidNpadButton_Y) out |= SWITCH_INPUT_Y;
    if (raw & HidNpadButton_RightSL) out |= SWITCH_INPUT_L;
    if (raw & HidNpadButton_RightSR) out |= SWITCH_INPUT_R;
    if (raw & HidNpadButton_Plus) out |= SWITCH_INPUT_PLUS;
    if (raw & HidNpadButton_StickR) out |= SWITCH_INPUT_STICK_L;
    return out;
}

static void map_axes(const PadState *pad, SwitchInputPad *state) {
    HidAnalogStickState left = padGetStickPos(pad, 0);
    HidAnalogStickState right = padGetStickPos(pad, 1);

    if (state->style == SWITCH_INPUT_STYLE_JOY_LEFT) {
        /* Rotate the left Joy-Con stick into horizontal-controller space. */
        state->left_x = clamp_axis(left.y);
        state->left_y = clamp_axis(-left.x);
        state->right_x = 0;
        state->right_y = 0;
    } else if (state->style == SWITCH_INPUT_STYLE_JOY_RIGHT) {
        /* The single right Joy-Con uses the right stick and opposite rotation. */
        state->left_x = clamp_axis(-right.y);
        state->left_y = clamp_axis(right.x);
        state->right_x = 0;
        state->right_y = 0;
    } else {
        state->left_x = clamp_axis(left.x);
        state->left_y = clamp_axis(left.y);
        state->right_x = clamp_axis(right.x);
        state->right_y = clamp_axis(right.y);
    }
}

static HidNpadStyleTag vibration_style(const PadState *pad) {
    const uint32_t style = padGetStyleSet(pad);
    if (padIsHandheld(pad) || (style & HidNpadStyleTag_NpadHandheld)) return HidNpadStyleTag_NpadHandheld;
    if (style & HidNpadStyleTag_NpadFullKey) return HidNpadStyleTag_NpadFullKey;
    if (style & HidNpadStyleTag_NpadJoyDual) return HidNpadStyleTag_NpadJoyDual;
    if (style & HidNpadStyleTag_NpadJoyLeft) return HidNpadStyleTag_NpadJoyLeft;
    if (style & HidNpadStyleTag_NpadJoyRight) return HidNpadStyleTag_NpadJoyRight;
    return (HidNpadStyleTag)0;
}

static void invalidate_vibration(uint8_t slot) {
    sVibrationHandleCount[slot] = 0;
    sVibrationStyle[slot] = 0;
}

static bool ensure_vibration(uint8_t slot) {
    if (slot >= SWITCH_INPUT_MAX_PLAYERS || !sStates[slot].connected) return false;

    const HidNpadStyleTag style = vibration_style(&sPads[slot]);
    if (style == 0) return false;

    if (sVibrationHandleCount[slot] > 0 && sVibrationStyle[slot] == (uint32_t)style) {
        return true;
    }

    invalidate_vibration(slot);

    const HidNpadIdType id = (slot == 0 && padIsHandheld(&sPads[slot]))
        ? HidNpadIdType_Handheld
        : sNpadIds[slot];
    const int handle_count =
        (style == HidNpadStyleTag_NpadJoyLeft || style == HidNpadStyleTag_NpadJoyRight) ? 1 : 2;

    Result rc = hidInitializeVibrationDevices(sVibrationHandles[slot], handle_count, id, style);
    if (R_FAILED(rc)) return false;

    sVibrationHandleCount[slot] = (uint8_t)handle_count;
    sVibrationStyle[slot] = (uint32_t)style;
    return true;
}

bool switch_input_init(void) {
    if (sInitialized) return true;

    memset(sPads, 0, sizeof(sPads));
    memset(sStates, 0, sizeof(sStates));
    memset(sVibrationHandles, 0, sizeof(sVibrationHandles));
    memset(sVibrationHandleCount, 0, sizeof(sVibrationHandleCount));
    memset(sVibrationStyle, 0, sizeof(sVibrationStyle));

    padConfigureInput(
        SWITCH_INPUT_MAX_PLAYERS,
        HidNpadStyleSet_NpadStandard |
        HidNpadStyleTag_NpadGc |
        HidNpadStyleTag_NpadLagon |
        HidNpadStyleTag_NpadSystemExt
    );

    padInitialize(&sPads[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    padInitialize(&sPads[1], HidNpadIdType_No2);
    padInitialize(&sPads[2], HidNpadIdType_No3);
    padInitialize(&sPads[3], HidNpadIdType_No4);

    sInitialized = true;
    switch_input_poll();
    return true;
}

void switch_input_shutdown(void) {
    if (!sInitialized) return;
    switch_input_rumble_stop_all();
    memset(sStates, 0, sizeof(sStates));
    sInitialized = false;
}

void switch_input_poll(void) {
    if (!sInitialized) return;

    for (uint8_t i = 0; i < SWITCH_INPUT_MAX_PLAYERS; i++) {
        SwitchInputPad previous = sStates[i];
        padUpdate(&sPads[i]);

        SwitchInputPad *state = &sStates[i];
        memset(state, 0, sizeof(*state));
        state->connected = padIsConnected(&sPads[i]);
        if (!state->connected) {
            if (previous.connected) invalidate_vibration(i);
            continue;
        }

        state->style = get_style(&sPads[i]);
        const uint64_t raw = padGetButtons(&sPads[i]);
        if (state->style == SWITCH_INPUT_STYLE_JOY_LEFT) {
            state->buttons = map_single_left_buttons(raw);
        } else if (state->style == SWITCH_INPUT_STYLE_JOY_RIGHT) {
            state->buttons = map_single_right_buttons(raw);
        } else {
            state->buttons = map_full_buttons(raw);
        }

        state->buttons_down = ~previous.buttons & state->buttons;
        state->buttons_up = previous.buttons & ~state->buttons;
        map_axes(&sPads[i], state);

        if (previous.style != state->style) invalidate_vibration(i);
    }
}

uint8_t switch_input_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < SWITCH_INPUT_MAX_PLAYERS; i++) {
        if (sStates[i].connected) count++;
    }
    return count;
}

bool switch_input_get(uint8_t slot, SwitchInputPad *out) {
    if (!sInitialized || out == NULL || slot >= SWITCH_INPUT_MAX_PLAYERS) return false;
    *out = sStates[slot];
    return out->connected;
}

bool switch_input_rumble(uint8_t slot, float strength) {
    if (!sInitialized || slot >= SWITCH_INPUT_MAX_PLAYERS) return false;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    if (!ensure_vibration(slot)) return false;

    HidVibrationValue values[2];
    memset(values, 0, sizeof(values));
    for (uint8_t i = 0; i < sVibrationHandleCount[slot]; i++) {
        values[i].freq_low = 160.0f;
        values[i].freq_high = 320.0f;
        values[i].amp_low = strength;
        values[i].amp_high = strength;
    }

    return R_SUCCEEDED(hidSendVibrationValues(
        sVibrationHandles[slot], values, sVibrationHandleCount[slot]
    ));
}

void switch_input_rumble_stop(uint8_t slot) {
    if (!sInitialized || slot >= SWITCH_INPUT_MAX_PLAYERS || sVibrationHandleCount[slot] == 0) return;

    HidVibrationValue values[2];
    memset(values, 0, sizeof(values));
    for (uint8_t i = 0; i < sVibrationHandleCount[slot]; i++) {
        values[i].freq_low = 160.0f;
        values[i].freq_high = 320.0f;
    }
    hidSendVibrationValues(sVibrationHandles[slot], values, sVibrationHandleCount[slot]);
}

void switch_input_rumble_stop_all(void) {
    for (uint8_t i = 0; i < SWITCH_INPUT_MAX_PLAYERS; i++) {
        switch_input_rumble_stop(i);
    }
}

#endif /* __SWITCH__ */
