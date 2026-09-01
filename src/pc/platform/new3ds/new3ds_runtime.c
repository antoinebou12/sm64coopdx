#include "new3ds_runtime.h"

#include <math.h>
#include <string.h>

bool new3ds_runtime_init(New3dsRuntimeState *state) {
    if (state == NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));

    if (R_SUCCEEDED(APT_CheckNew3DS(&state->is_new_3ds)) && state->is_new_3ds) {
        osSetSpeedupEnable(true);
        state->speedup_enabled = true;
    }

    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    state->start_ms = osGetTime();
    state->initialized = true;
    return true;
}

void new3ds_runtime_shutdown(New3dsRuntimeState *state) {
    if (state == NULL || !state->initialized) {
        return;
    }

    if (state->speedup_enabled) {
        osSetSpeedupEnable(false);
    }

    memset(&state->input, 0, sizeof(state->input));
    state->initialized = false;
}

bool new3ds_runtime_poll(New3dsRuntimeState *state) {
    if (state == NULL || !state->initialized || state->exit_requested) {
        return false;
    }
    if (!aptMainLoop()) {
        state->exit_requested = true;
        return false;
    }

    hidScanInput();
    state->input.down = hidKeysDown();
    state->input.held = hidKeysHeld();
    state->input.up = hidKeysUp();
    hidCircleRead(&state->input.circle);

    if (state->is_new_3ds) {
        hidCstickRead(&state->input.cstick);
    } else {
        state->input.cstick.dx = 0;
        state->input.cstick.dy = 0;
    }

    state->input.touching = (state->input.held & KEY_TOUCH) != 0;
    if (state->input.touching) {
        hidTouchRead(&state->input.touch);
    } else {
        state->input.touch.px = 0;
        state->input.touch.py = 0;
    }

    state->frame_index++;
    return true;
}

void new3ds_runtime_request_exit(New3dsRuntimeState *state) {
    if (state != NULL) {
        state->exit_requested = true;
    }
}

uint64_t new3ds_runtime_time_ms(void) {
    return osGetTime();
}

double new3ds_runtime_time_seconds(void) {
    return (double)osGetTime() / 1000.0;
}

void new3ds_runtime_sleep_ms(uint32_t ms) {
    svcSleepThread((s64)ms * 1000000LL);
}

float new3ds_runtime_axis_normalized(s16 value) {
    const float normalized = (float)value / 156.0f;
    if (normalized < -1.0f) {
        return -1.0f;
    }
    if (normalized > 1.0f) {
        return 1.0f;
    }
    if (fabsf(normalized) < 0.08f) {
        return 0.0f;
    }
    return normalized;
}
