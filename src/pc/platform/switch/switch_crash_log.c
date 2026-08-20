#ifdef __SWITCH__

#include "switch_crash_log.h"
#include "switch_rom_asset_trace.h"

#include <switch.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SWITCH_CRASH_ROOT "sdmc:/switch/sm64coopdx"
#define SWITCH_CRASH_LOG_DIR SWITCH_CRASH_ROOT "/logs"
#define SWITCH_STARTUP_LOG SWITCH_CRASH_LOG_DIR "/startup.log"
#define SWITCH_EXCEPTION_LOG SWITCH_CRASH_LOG_DIR "/exception.log"
#define SWITCH_LAST_CHECKPOINT SWITCH_CRASH_LOG_DIR "/last_checkpoint.txt"
#define SWITCH_BASEROM_US SWITCH_CRASH_ROOT "/baserom.us.z64"

static bool sLogReady = false;
static char sLastCheckpoint[160] = "libnx: before userAppInit";

extern int main(int argc, char **argv);

static bool switch_crash_log_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool switch_crash_log_prepare(void) {
    if (sLogReady) {
        return true;
    }

    if (!switch_crash_log_mkdir("sdmc:/switch")) {
        return false;
    }
    if (!switch_crash_log_mkdir(SWITCH_CRASH_ROOT)) {
        return false;
    }
    if (!switch_crash_log_mkdir(SWITCH_CRASH_LOG_DIR)) {
        return false;
    }

    sLogReady = true;
    return true;
}

static void switch_crash_log_commit(void) {
    (void)fsdevCommitDevice("sdmc");
}

static long long switch_crash_log_epoch(void) {
    const time_t now = time(NULL);
    return (long long)now;
}

static void switch_crash_log_debug_string(const char *text) {
    if (text == NULL) {
        return;
    }
    (void)svcOutputDebugString(text, strlen(text));
}

const char *switch_crash_log_directory(void) {
    return SWITCH_CRASH_LOG_DIR;
}

void switch_crash_log_printf(const char *fmt, ...) {
    if (fmt == NULL || !switch_crash_log_prepare()) {
        return;
    }

    FILE *file = fopen(SWITCH_STARTUP_LOG, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "[%lld] ", switch_crash_log_epoch());

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    fputc('\n', file);
    fflush(file);
    fclose(file);
    switch_crash_log_commit();
}

void switch_crash_log_checkpoint(const char *checkpoint) {
    if (checkpoint == NULL) {
        checkpoint = "(null)";
    }

    snprintf(sLastCheckpoint, sizeof(sLastCheckpoint), "%s", checkpoint);
    switch_crash_log_printf("checkpoint=%s", sLastCheckpoint);

    if (!switch_crash_log_prepare()) {
        return;
    }

    FILE *file = fopen(SWITCH_LAST_CHECKPOINT, "w");
    if (file != NULL) {
        fprintf(file, "epoch=%lld\ncheckpoint=%s\n", switch_crash_log_epoch(), sLastCheckpoint);
        fflush(file);
        fclose(file);
        switch_crash_log_commit();
    }
}

static bool switch_crash_log_rom_state(long long *size_out) {
    struct stat st;
    if (stat(SWITCH_BASEROM_US, &st) != 0) {
        if (size_out != NULL) {
            *size_out = -1;
        }
        return false;
    }

    if (size_out != NULL) {
        *size_out = (long long)st.st_size;
    }
    return S_ISREG(st.st_mode);
}

void userAppInit(void) {
    if (!switch_crash_log_prepare()) {
        switch_crash_log_debug_string("SM64CoopDX: could not create sdmc crash log directory\n");
        return;
    }

    long long rom_size = -1;
    const bool rom_present = switch_crash_log_rom_state(&rom_size);
    const AppletType applet_type = appletGetAppletType();

    FILE *file = fopen(SWITCH_STARTUP_LOG, "a");
    if (file != NULL) {
        fprintf(file, "\n=== SM64CoopDX Switch launch ===\n");
        fprintf(file, "epoch=%lld\n", switch_crash_log_epoch());
        fprintf(file, "hos_version=0x%08" PRIx32 "\n", hosversionGet());
        fprintf(file, "applet_type=%d\n", (int)applet_type);
        fprintf(file, "application_mode=%s\n",
                (applet_type == AppletType_Application || applet_type == AppletType_SystemApplication) ? "yes" : "no");
        fprintf(file, "runtime_main=%p\n", (void *)&main);
        fprintf(file, "runtime_userAppInit=%p\n", (void *)&userAppInit);
        fprintf(file, "baserom_path=%s\n", SWITCH_BASEROM_US);
        fprintf(file, "baserom_present=%s\n", rom_present ? "yes" : "no");
        fprintf(file, "baserom_size=%lld\n", rom_size);
        fflush(file);
        fclose(file);
        switch_crash_log_commit();
    }

    switch_crash_log_checkpoint(rom_present
        ? "userAppInit: baserom.us.z64 present"
        : "userAppInit: baserom.us.z64 missing");
    switch_crash_log_debug_string("SM64CoopDX: Switch crash logger active\n");
}

static void switch_crash_log_constructor(void) __attribute__((constructor(101)));
static void switch_crash_log_constructor(void) {
    switch_crash_log_checkpoint("constructor: runtime initialized");
    switch_crash_log_printf("constructor=crash logger linked and running");
}

void userAppExit(void) {
    switch_crash_log_checkpoint("userAppExit: clean shutdown");
    switch_crash_log_printf("shutdown=clean");
}

/*
 * libnx supports overriding this weak hook for userland exception handling.
 * Keep the handler self-contained: write the raw register state first so a
 * crash can be symbolized against build/us_switch/sm64coopdx.map later.
 */
__attribute__((aligned(16))) unsigned char __nx_exception_stack[0x4000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    switch_crash_log_debug_string("SM64CoopDX: fatal exception captured\n");

    /* Preserve the exact in-memory ROM-asset event before doing more work. */
    switch_rom_asset_trace_exception_snapshot();

    if (ctx == NULL || !switch_crash_log_prepare()) {
        return;
    }

    FILE *file = fopen(SWITCH_EXCEPTION_LOG, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "\n=== fatal exception ===\n");
    fprintf(file, "epoch=%lld\n", switch_crash_log_epoch());
    fprintf(file, "last_checkpoint=%s\n", sLastCheckpoint);
    fprintf(file, "rom_asset_failure_events=%u\n", switch_rom_asset_trace_failure_count());
    fprintf(file, "runtime_main=%p\n", (void *)&main);
    fprintf(file, "error_desc=0x%08" PRIx32 "\n", ctx->error_desc);
    fprintf(file, "pc=0x%016" PRIx64 "\n", (uint64_t)ctx->pc.x);
    fprintf(file, "lr=0x%016" PRIx64 "\n", (uint64_t)ctx->lr.x);
    fprintf(file, "sp=0x%016" PRIx64 "\n", (uint64_t)ctx->sp.x);
    fprintf(file, "fp=0x%016" PRIx64 "\n", (uint64_t)ctx->fp.x);
    fprintf(file, "far=0x%016" PRIx64 "\n", (uint64_t)ctx->far.x);
    fprintf(file, "esr=0x%08" PRIx32 "\n", ctx->esr);
    fprintf(file, "pstate=0x%08" PRIx32 "\n", ctx->pstate);
    fprintf(file, "afsr0=0x%08" PRIx32 "\n", ctx->afsr0);
    fprintf(file, "afsr1=0x%08" PRIx32 "\n", ctx->afsr1);

    for (int i = 0; i < 29; ++i) {
        fprintf(file, "x%d=0x%016" PRIx64 "\n", i, (uint64_t)ctx->cpu_gprs[i].x);
    }

    fflush(file);
    fclose(file);
    switch_crash_log_commit();
}

/*
 * The SD-layout debug build links these through GNU ld --wrap. This keeps the
 * upstream startup code untouched while giving the ROM-present crash path
 * precise before/after checkpoints that survive on the SD card.
 */
extern bool __real_main_rom_handler(void);
extern void __real_rom_assets_load(void);
extern void __real_mods_init(void);
extern void __real_audio_init(void);
extern void __real_sound_init(void);
extern void __real_network_player_init(void);
extern void __real_thread5_game_loop(void *arg);

bool __wrap_main_rom_handler(void) {
    switch_crash_log_checkpoint("rom validation: begin");
    const bool valid = __real_main_rom_handler();
    switch_crash_log_checkpoint(valid
        ? "rom validation: complete (valid)"
        : "rom validation: complete (not ready)");
    return valid;
}

void __wrap_rom_assets_load(void) {
    switch_crash_log_checkpoint("rom assets: load begin");
    __real_rom_assets_load();

    const unsigned int failure_events = switch_rom_asset_trace_failure_count();
    switch_crash_log_printf("rom_assets_failure_events=%u", failure_events);
    switch_crash_log_checkpoint(failure_events == 0
        ? "rom assets: load complete"
        : "rom assets: load complete with diagnostic failures");
}

void __wrap_mods_init(void) {
    switch_crash_log_checkpoint("mods: init begin");
    __real_mods_init();
    switch_crash_log_checkpoint("mods: init complete");
}

void __wrap_audio_init(void) {
    switch_crash_log_checkpoint("audio: core init begin");
    __real_audio_init();
    switch_crash_log_checkpoint("audio: core init complete");
}

void __wrap_sound_init(void) {
    switch_crash_log_checkpoint("audio: sound init begin");
    __real_sound_init();
    switch_crash_log_checkpoint("audio: sound init complete");
}

void __wrap_network_player_init(void) {
    switch_crash_log_checkpoint("network player: init begin");
    __real_network_player_init();
    switch_crash_log_checkpoint("network player: init complete");
}

void __wrap_thread5_game_loop(void *arg) {
    switch_crash_log_checkpoint("thread5 game bootstrap: begin");
    __real_thread5_game_loop(arg);
    switch_crash_log_checkpoint("thread5 game bootstrap: complete");
}

#endif /* __SWITCH__ */
