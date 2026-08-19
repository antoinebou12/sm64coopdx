#ifdef __SWITCH__

#include "switch_platform.h"

#include <switch.h>
#include <string.h>

static AppletHookCookie sAppletHookCookie;
static SwitchPlatformState *sState = NULL;

static void switch_platform_refresh_state(SwitchPlatformState *state) {
    if (state == NULL) {
        return;
    }

    state->docked = appletGetOperationMode() == AppletOperationMode_Console;
    state->suspended = appletGetFocusState() == AppletFocusState_Background;
}

static void switch_platform_applet_hook(AppletHookType hook, void *param) {
    SwitchPlatformState *state = (SwitchPlatformState *)param;
    if (state == NULL) {
        return;
    }

    switch (hook) {
        case AppletHookType_OnFocusState:
            state->last_event = SWITCH_LIFECYCLE_FOCUS_CHANGED;
            switch_platform_refresh_state(state);
            break;
        case AppletHookType_OnOperationMode:
            state->last_event = SWITCH_LIFECYCLE_OPERATION_MODE_CHANGED;
            switch_platform_refresh_state(state);
            break;
        case AppletHookType_OnPerformanceMode:
            state->last_event = SWITCH_LIFECYCLE_PERFORMANCE_MODE_CHANGED;
            break;
        case AppletHookType_OnExitRequest:
            state->last_event = SWITCH_LIFECYCLE_EXIT_REQUESTED;
            state->exit_requested = true;
            break;
        case AppletHookType_OnResume:
            state->last_event = SWITCH_LIFECYCLE_RESUMED;
            state->suspended = false;
            switch_platform_refresh_state(state);
            break;
        default:
            break;
    }
}

bool switch_platform_init(SwitchPlatformState *state) {
    if (state == NULL || sState != NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));

    const Result socket_rc = socketInitializeDefault();
    if (R_FAILED(socket_rc)) {
        return false;
    }
    state->socket_initialized = true;

    const AppletType applet_type = appletGetAppletType();
    state->application_mode = applet_type == AppletType_Application ||
                              applet_type == AppletType_SystemApplication;
    state->hos_version = hosversionGet();
    state->initialized = true;
    state->last_event = SWITCH_LIFECYCLE_NONE;

    switch_platform_refresh_state(state);
    appletHook(&sAppletHookCookie, switch_platform_applet_hook, state);
    sState = state;

    return true;
}

void switch_platform_shutdown(SwitchPlatformState *state) {
    if (state == NULL || !state->initialized || sState != state) {
        return;
    }

    appletUnhook(&sAppletHookCookie);

    if (state->socket_initialized) {
        socketExit();
        state->socket_initialized = false;
    }

    state->initialized = false;
    sState = NULL;
}

bool switch_platform_main_loop(SwitchPlatformState *state) {
    if (state == NULL || !state->initialized || state->exit_requested) {
        return false;
    }

    if (!appletMainLoop()) {
        state->exit_requested = true;
        state->last_event = SWITCH_LIFECYCLE_EXIT_REQUESTED;
        return false;
    }

    switch_platform_refresh_state(state);
    return !state->exit_requested;
}

const char *switch_platform_data_root(void) {
    return "sdmc:/switch/sm64coopdx";
}

#endif /* __SWITCH__ */