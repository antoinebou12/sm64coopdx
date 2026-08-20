#ifdef __SWITCH__

#include "switch_crash_log.h"

#include <switch.h>
#include <stdlib.h>

/*
 * InfoType_TotalMemorySize/UsedMemorySize report what libnx RESERVED via
 * svcSetHeapSize, not what's actually allocated - on Horizon the reservation
 * happens up front, so "used" sits close to "total" from the very first
 * launch regardless of real malloc headroom. That made every prior log read
 * as "3 MiB free" when the process was, in fact, in full application mode
 * with 3+ GiB available. Report both, but lead with an allocation ladder,
 * which measures the thing that actually matters: how large a single malloc
 * can still succeed right now.
 */
static void switch_startup_log_alloc_ladder(void) {
    static const size_t kRungsMiB[] = { 512, 256, 128, 64, 32, 16, 8, 4, 1 };
    size_t largest_ok_mib = 0;
    for (size_t i = 0; i < sizeof(kRungsMiB) / sizeof(kRungsMiB[0]); ++i) {
        void *p = malloc(kRungsMiB[i] * 1024ull * 1024ull);
        if (p != NULL) {
            free(p);
            largest_ok_mib = kRungsMiB[i];
            break;
        }
    }
    switch_crash_log_printf("memory alloc_ladder largest_ok_mib=%zu", largest_ok_mib);
}

static void switch_startup_log_heap_regions(void) {
    u64 heap_size = 0, alias_size = 0;
    const Result heap_rc = svcGetInfo(&heap_size, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    const Result alias_rc = svcGetInfo(&alias_size, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(heap_rc) && R_SUCCEEDED(alias_rc)) {
        switch_crash_log_printf("memory heap_region_mib=%llu alias_region_mib=%llu",
            (unsigned long long)(heap_size >> 20), (unsigned long long)(alias_size >> 20));
    } else {
        switch_crash_log_printf("memory heap region query failed heap_rc=0x%08x alias_rc=0x%08x",
            (unsigned int)heap_rc, (unsigned int)alias_rc);
    }
}

/*
 * Exported so other checkpoints (before/after the ROM asset load, and the
 * exit-trace wrappers) can take the same reading at a moment they choose,
 * not just once at startup.
 */
void switch_startup_memory_probe(const char *label) {
    switch_crash_log_printf("memory probe label=%s", label != NULL ? label : "(null)");
    switch_startup_log_alloc_ladder();
    switch_startup_log_heap_regions();

    u64 total_memory = 0;
    u64 used_memory = 0;
    const Result total_rc = svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const Result used_rc = svcGetInfo(&used_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    if (R_SUCCEEDED(total_rc) && R_SUCCEEDED(used_rc)) {
        const u64 free_memory = total_memory > used_memory ? total_memory - used_memory : 0;
        /*
         * NOTE: these are svc-reserved figures, not live allocator usage -
         * svc_used sits close to svc_total from process start regardless of
         * real headroom. Trust the alloc_ladder line above instead.
         */
        switch_crash_log_printf(
            "memory svc_total=%llu MiB svc_used=%llu MiB (reserved, not allocated) svc_free=%llu MiB",
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
 * Snapshot process memory before the ROM-present startup path begins doing the
 * heavy asset/mod/audio work. This is especially useful for detecting launches
 * in Horizon applet mode where available memory is much smaller.
 */
static void switch_startup_memory_snapshot(void) __attribute__((constructor(102)));
static void switch_startup_memory_snapshot(void) {
    switch_startup_memory_probe("startup");
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
