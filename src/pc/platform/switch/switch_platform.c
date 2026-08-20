#ifdef __SWITCH__

#include "switch_platform.h"
#include "switch_crash_log.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>

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

static const char *switch_platform_hook_name(AppletHookType hook) {
    switch (hook) {
        case AppletHookType_OnFocusState:       return "OnFocusState";
        case AppletHookType_OnOperationMode:    return "OnOperationMode";
        case AppletHookType_OnPerformanceMode:  return "OnPerformanceMode";
        case AppletHookType_OnExitRequest:      return "OnExitRequest";
        case AppletHookType_OnResume:           return "OnResume";
        default:                                return "Other";
    }
}

/*
 * libnx dispatches applet hooks from its own dedicated message-processing
 * thread, independent of whatever the game thread is doing - so this can
 * fire while rom_assets_load() is still blocking the game thread on the
 * loading screen. Log every hook, with a timestamp and the current focus/
 * operation mode, BEFORE calling sExitCallback() on OnExitRequest: if the
 * process exits from here, this is the last thing that will be on record.
 */
static void switch_platform_applet_hook(AppletHookType hook, void *param) {
    SwitchPlatformState *state = (SwitchPlatformState *)param;
    if (state == NULL) {
        return;
    }

    switch_crash_log_printf(
        "applet hook=%s (%d) focus=%d op_mode=%d",
        switch_platform_hook_name(hook), (int)hook,
        (int)appletGetFocusState(), (int)appletGetOperationMode());

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
            switch_crash_log_printf("applet OnExitRequest -> invoking exit callback");
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
     * CoopDX uses BSD sockets directly. On Horizon those calls require the
     * libnx socket service to be initialized first. Keep platform startup
     * alive if the service is unavailable so offline play still works.
     */
    sSocketsInitialized = R_SUCCEEDED(socketInitializeDefault());

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

/*
 * This is a faithful decomposition of libnx's appletMainLoop(), which
 * internally does exactly this pump-and-check loop but swallows the message
 * value. appletProcessMessage() returning false is precisely the condition
 * under which appletMainLoop() itself returns false, so logging around it
 * changes no behavior - it only makes the "why" visible on the SD card.
 */
bool switch_platform_main_loop(SwitchPlatformState *state) {
    if (state == NULL || !state->initialized || state->exit_requested) {
        return false;
    }

    u32 msg = 0;
    while (R_SUCCEEDED(appletGetMessage(&msg))) {
        switch_crash_log_printf("applet msg=%u focus=%d op_mode=%d",
            (unsigned int)msg, (int)appletGetFocusState(), (int)appletGetOperationMode());
        if (!appletProcessMessage(msg)) {
            switch_crash_log_printf("applet msg=%u requested exit", (unsigned int)msg);
            state->exit_requested = true;
            state->last_event = SWITCH_LIFECYCLE_EXIT_REQUESTED;
            return false;
        }
    }

    switch_platform_refresh_state(state);
    return !state->exit_requested;
}

const char *switch_platform_data_root(void) {
    return "sdmc:/switch/sm64coopdx";
}

#endif /* __SWITCH__ */
