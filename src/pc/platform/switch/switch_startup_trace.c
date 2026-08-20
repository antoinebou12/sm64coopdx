#ifdef __SWITCH__

#include "switch_crash_log.h"

#include <switch.h>

/*
 * Snapshot process memory before the ROM-present startup path begins doing the
 * heavy asset/mod/audio work. This is especially useful for detecting launches
 * in Horizon applet mode where available memory is much smaller.
 */
static void switch_startup_memory_snapshot(void) __attribute__((constructor(102)));
static void switch_startup_memory_snapshot(void) {
    u64 total_memory = 0;
    u64 used_memory = 0;
    const Result total_rc = svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const Result used_rc = svcGetInfo(&used_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    if (R_SUCCEEDED(total_rc) && R_SUCCEEDED(used_rc)) {
        const u64 free_memory = total_memory > used_memory ? total_memory - used_memory : 0;
        switch_crash_log_printf(
            "memory total=%llu MiB used=%llu MiB free=%llu MiB",
            (unsigned long long)(total_memory >> 20),
            (unsigned long long)(used_memory >> 20),
            (unsigned long long)(free_memory >> 20));
    } else {
        switch_crash_log_printf(
            "memory query failed total_rc=0x%08x used_rc=0x%08x",
            (unsigned int)total_rc,
            (unsigned int)used_rc);
    }
}

/*
 * These functions are reached immediately after thread5_game_loop() during
 * Switch startup. The debug SD-layout build redirects them through GNU ld
 * --wrap so an instant crash leaves a durable last-known stage on the SD card.
 */
extern void __real_djui_init(void);
extern void __real_djui_unicode_init(void);
extern void __real_djui_init_late(void);

void __wrap_djui_init(void) {
    switch_crash_log_checkpoint("djui: init begin");
    __real_djui_init();
    switch_crash_log_checkpoint("djui: init complete");
}

void __wrap_djui_unicode_init(void) {
    switch_crash_log_checkpoint("djui: unicode init begin");
    __real_djui_unicode_init();
    switch_crash_log_checkpoint("djui: unicode init complete");
}

void __wrap_djui_init_late(void) {
    switch_crash_log_checkpoint("djui: late init begin");
    __real_djui_init_late();
    switch_crash_log_checkpoint("djui: late init complete");
}

#endif /* __SWITCH__ */
