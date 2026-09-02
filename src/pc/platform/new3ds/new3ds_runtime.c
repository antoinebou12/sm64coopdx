#include "new3ds_runtime.h"

#include "new3ds_log.h"

#include <arpa/inet.h>
#include <malloc.h>
#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEW3DS_SOC_BUFFER_SIZE 0x100000
#define NEW3DS_SOC_BUFFER_ALIGN 0x1000

static New3dsRuntimeState *sActiveRuntime = NULL;

static void new3ds_runtime_network_init(New3dsRuntimeState *state) {
    if (state == NULL || !state->is_new_3ds || state->soc_initialized) return;

    state->soc_buffer = memalign(NEW3DS_SOC_BUFFER_ALIGN, NEW3DS_SOC_BUFFER_SIZE);
    if (state->soc_buffer == NULL) {
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "runtime", "SOC buffer alloc failed");
        return;
    }

    const Result rc = socInit((u32 *)state->soc_buffer, NEW3DS_SOC_BUFFER_SIZE);
    if (R_FAILED(rc)) {
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_NET, "runtime", "socInit failed rc=0x%08lX", (unsigned long)rc);
        free(state->soc_buffer);
        state->soc_buffer = NULL;
        return;
    }

    state->soc_initialized = true;
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "runtime", "SOC initialized (%u bytes)", NEW3DS_SOC_BUFFER_SIZE);
}

static void new3ds_runtime_network_shutdown(New3dsRuntimeState *state) {
    if (state == NULL) return;

    if (state->soc_initialized) {
        socExit();
        state->soc_initialized = false;
        NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "runtime", "SOC shutdown");
    }

    if (state->soc_buffer != NULL) {
        free(state->soc_buffer);
        state->soc_buffer = NULL;
    }
}

bool new3ds_runtime_init(New3dsRuntimeState *state) {
    if (state == NULL || sActiveRuntime != NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));

    if (R_SUCCEEDED(APT_CheckNew3DS(&state->is_new_3ds)) && state->is_new_3ds) {
        osSetSpeedupEnable(true);
        state->speedup_enabled = true;
        NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "runtime", "New 3DS detected, CPU speedup enabled");
    } else {
        NEW3DS_LOG_WARN_CAT(NEW3DS_LOG_CAT_RUNTIME, "runtime", "Unsupported hardware (New 3DS required)");
    }

    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    state->start_ms = osGetTime();
    state->initialized = true;
    sActiveRuntime = state;

    /* Networking is optional: offline gameplay must still boot if SOC fails. */
    new3ds_runtime_network_init(state);
    return true;
}

void new3ds_runtime_shutdown(New3dsRuntimeState *state) {
    if (state == NULL || !state->initialized) {
        return;
    }

    new3ds_runtime_network_shutdown(state);

    if (state->speedup_enabled) {
        osSetSpeedupEnable(false);
    }

    memset(&state->input, 0, sizeof(state->input));
    state->initialized = false;
    if (sActiveRuntime == state) {
        sActiveRuntime = NULL;
    }
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

New3dsRuntimeState *new3ds_runtime_active(void) {
    return sActiveRuntime;
}

bool new3ds_runtime_network_available(void) {
    return sActiveRuntime != NULL && sActiveRuntime->initialized && sActiveRuntime->soc_initialized;
}

bool new3ds_runtime_get_ipv4_string(char *buf, size_t len) {
    if (buf == NULL || len == 0) {
        return false;
    }

    if (!new3ds_runtime_network_available()) {
        snprintf(buf, len, "No network");
        return false;
    }

    const u32 host = gethostid();
    if (host == 0) {
        snprintf(buf, len, "No network");
        return false;
    }

    struct in_addr addr;
    addr.s_addr = host;
    const char *text = inet_ntoa(addr);
    if (text == NULL || text[0] == '\0') {
        snprintf(buf, len, "No network");
        return false;
    }

    snprintf(buf, len, "%s", text);
    return true;
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
