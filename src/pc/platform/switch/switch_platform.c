#ifdef __SWITCH__

#include "switch_platform.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>

#include "pc/platform/switch/switch_crash_log.h"

static AppletHookCookie sAppletHookCookie;
static SwitchPlatformState *sState = NULL;
static SwitchPlatformExitFn sExitCallback = NULL;
static bool sSocketsInitialized = false;

typedef struct SwitchMainThreadContext {
    SwitchPlatformMainFn entry;
    int argc;
    char **argv;
    int result;
} SwitchMainThreadContext;

static void switch_platform_main_thread_entry(void *arg) {
    SwitchMainThreadContext *context = (SwitchMainThreadContext *)arg;
    context->result = context->entry(context->argc, context->argv);
}

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
            if (sExitCallback != NULL) {
                sExitCallback();
            }
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

int switch_platform_run_main_on_game_thread(SwitchPlatformMainFn entry, int argc, char **argv) {
    if (entry == NULL) {
        return -1;
    }

    SwitchMainThreadContext context = {
        .entry = entry,
        .argc = argc,
        .argv = argv,
        .result = -1,
    };

    Thread thread;
    Result rc = threadCreate(
        &thread,
        switch_platform_main_thread_entry,
        &context,
        NULL,
        SWITCH_PLATFORM_GAME_STACK_SIZE,
        0x2C,
        -2
    );
    if (R_FAILED(rc)) {
        return -1;
    }

    rc = threadStart(&thread);
    if (R_FAILED(rc)) {
        threadClose(&thread);
        return -1;
    }

    threadWaitForExit(&thread);
    threadClose(&thread);
    return context.result;
}

void switch_platform_set_exit_callback(SwitchPlatformExitFn callback) {
    sExitCallback = callback;
}

bool switch_platform_init(SwitchPlatformState *state) {
    if (state == NULL || sState != NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));

    const AppletType applet_type = appletGetAppletType();
    state->application_mode = applet_type == AppletType_Application ||
                              applet_type == AppletType_SystemApplication;
    state->hos_version = hosversionGet();
    state->initialized = true;
    state->last_event = SWITCH_LIFECYCLE_NONE;

    if (!state->application_mode) {
        fprintf(stderr,
            "WARNING: SM64CoopDX is running in Horizon applet mode. "
            "Use title override/full application mode for the available memory required by the full game and larger mods.\n");
    }

    /*
     * Keep only BSD sockets alive during game bootstrap. A previous Switch-only
     * change eagerly initialized NIFM here and the first full ROM builds after
     * that change began faulting inside thread5_game_loop. Signaling worked on
     * hardware before that change with socketInitializeDefault() alone.
     *
     * NIFM is now initialized lazily by the CoopNet peer/ICE path, after the
     * game has completed its bootstrap. This keeps offline startup identical to
     * the last known-good path while still keeping NIFM alive for ICE once the
     * user actually hosts or joins a CoopNet lobby.
     */
    Result socketRc = socketInitializeDefault();
    sSocketsInitialized = R_SUCCEEDED(socketRc);

    switch_crash_log_printf(
        "network services socket_rc=0x%08x socket_ready=%d nifm=deferred",
        (unsigned int)socketRc,
        sSocketsInitialized ? 1 : 0);

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

    if (sSocketsInitialized) {
        socketExit();
        sSocketsInitialized = false;
    }

    state->initialized = false;
    sState = NULL;
    sExitCallback = NULL;
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
