#ifdef __SWITCH__

#include "switch_rom_asset_trace.h"

#include <switch.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SWITCH_ROM_TRACE_ROOT "sdmc:/switch/sm64coopdx"
#define SWITCH_ROM_TRACE_LOG_DIR SWITCH_ROM_TRACE_ROOT "/logs"
#define SWITCH_ROM_TRACE_CURRENT SWITCH_ROM_TRACE_LOG_DIR "/current_rom_asset.txt"
#define SWITCH_ROM_TRACE_EVENTS SWITCH_ROM_TRACE_LOG_DIR "/rom_assets.log"
#define SWITCH_ROM_TRACE_ERRORS SWITCH_ROM_TRACE_LOG_DIR "/rom_assets_errors.log"
#define SWITCH_ROM_TRACE_EXCEPTION SWITCH_ROM_TRACE_LOG_DIR "/rom_asset_exception_context.txt"

#define SWITCH_ROM_TRACE_MESSAGE_MAX 1536
#define SWITCH_ROM_TRACE_ASSET_COMMIT_INTERVAL 128u

static unsigned int sEventSequence = 0;
static unsigned int sAssetMarks = 0;
static unsigned int sFailureEvents = 0;
static char sLastEvent[SWITCH_ROM_TRACE_MESSAGE_MAX] = "phase=not started";
static char sLastAsset[SWITCH_ROM_TRACE_MESSAGE_MAX] = "phase=asset not started";

struct SwitchRomMemorySnapshot {
    u64 total;
    u64 used;
    u64 free;
    Result total_rc;
    Result used_rc;
};

static int switch_rom_trace_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    return errno == EEXIST;
}

static int switch_rom_trace_prepare(void) {
    return switch_rom_trace_mkdir("sdmc:/switch") &&
           switch_rom_trace_mkdir(SWITCH_ROM_TRACE_ROOT) &&
           switch_rom_trace_mkdir(SWITCH_ROM_TRACE_LOG_DIR);
}

static long long switch_rom_trace_epoch(void) {
    return (long long)time(NULL);
}

static struct SwitchRomMemorySnapshot switch_rom_trace_memory(void) {
    struct SwitchRomMemorySnapshot snapshot = { 0 };
    snapshot.total_rc = svcGetInfo(&snapshot.total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    snapshot.used_rc = svcGetInfo(&snapshot.used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(snapshot.total_rc) && R_SUCCEEDED(snapshot.used_rc) && snapshot.total > snapshot.used) {
        snapshot.free = snapshot.total - snapshot.used;
    }
    return snapshot;
}

static int switch_rom_trace_is_asset_begin(const char *message) {
    return message != NULL && strncmp(message, "phase=asset begin", strlen("phase=asset begin")) == 0;
}

static int switch_rom_trace_is_load_begin(const char *message) {
    return message != NULL && strncmp(message, "phase=rom open begin", strlen("phase=rom open begin")) == 0;
}

static int switch_rom_trace_is_failure(const char *message) {
    if (message == NULL) {
        return 0;
    }

    return strstr(message, " failed") != NULL ||
           strstr(message, " failure") != NULL ||
           strstr(message, " rejected") != NULL ||
           strstr(message, " overrun") != NULL ||
           strstr(message, "invalid backref") != NULL ||
           strstr(message, "bounds failure") != NULL ||
           strstr(message, "short read") != NULL;
}

static void switch_rom_trace_truncate(const char *path) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fclose(file);
}

static void switch_rom_trace_write_snapshot(FILE *file,
                                            const char *label,
                                            const char *message,
                                            const struct SwitchRomMemorySnapshot *memory) {
    if (file == NULL) {
        return;
    }

    fprintf(file, "epoch=%lld\n", switch_rom_trace_epoch());
    fprintf(file, "sequence=%u\n", sEventSequence);
    fprintf(file, "failure_events=%u\n", sFailureEvents);
    if (label != NULL) {
        fprintf(file, "snapshot=%s\n", label);
    }
    if (message != NULL) {
        fprintf(file, "%s\n", message);
    }
    fprintf(file, "last_asset:\n%s\n", sLastAsset);

    if (memory != NULL && R_SUCCEEDED(memory->total_rc) && R_SUCCEEDED(memory->used_rc)) {
        fprintf(file, "memory_total=%llu\n", (unsigned long long)memory->total);
        fprintf(file, "memory_used=%llu\n", (unsigned long long)memory->used);
        fprintf(file, "memory_free=%llu\n", (unsigned long long)memory->free);
        fprintf(file, "memory_total_mib=%llu\n", (unsigned long long)(memory->total >> 20));
        fprintf(file, "memory_used_mib=%llu\n", (unsigned long long)(memory->used >> 20));
        fprintf(file, "memory_free_mib=%llu\n", (unsigned long long)(memory->free >> 20));
    } else if (memory != NULL) {
        fprintf(file, "memory_query_failed_total_rc=0x%08x\n", (unsigned int)memory->total_rc);
        fprintf(file, "memory_query_failed_used_rc=0x%08x\n", (unsigned int)memory->used_rc);
    }
}

static void switch_rom_trace_append(const char *path,
                                    const char *label,
                                    const char *message,
                                    const struct SwitchRomMemorySnapshot *memory) {
    FILE *file = fopen(path, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "\n=== %s ===\n", label != NULL ? label : "rom asset event");
    switch_rom_trace_write_snapshot(file, label, message, memory);
    fflush(file);
    fclose(file);
}

void switch_rom_asset_trace_printf(const char *fmt, ...) {
    if (fmt == NULL || !switch_rom_trace_prepare()) {
        return;
    }

    char message[SWITCH_ROM_TRACE_MESSAGE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    const int asset_begin = switch_rom_trace_is_asset_begin(message);
    const int load_begin = switch_rom_trace_is_load_begin(message);
    const int failure = switch_rom_trace_is_failure(message);

    if (load_begin) {
        sEventSequence = 0;
        sAssetMarks = 0;
        sFailureEvents = 0;
        snprintf(sLastAsset, sizeof(sLastAsset), "%s", "phase=asset not started");
        switch_rom_trace_truncate(SWITCH_ROM_TRACE_EVENTS);
        switch_rom_trace_truncate(SWITCH_ROM_TRACE_ERRORS);
        switch_rom_trace_truncate(SWITCH_ROM_TRACE_EXCEPTION);
    }

    sEventSequence++;
    snprintf(sLastEvent, sizeof(sLastEvent), "%s", message);

    if (asset_begin) {
        sAssetMarks++;
        snprintf(sLastAsset, sizeof(sLastAsset), "%s", message);
    }

    if (failure) {
        sFailureEvents++;
    }

    const int sampled_asset = asset_begin &&
        (sAssetMarks == 1u || (sAssetMarks % SWITCH_ROM_TRACE_ASSET_COMMIT_INTERVAL) == 0u);
    const int durable_event = !asset_begin || failure || sampled_asset;
    struct SwitchRomMemorySnapshot memory = { 0 };
    if (durable_event) {
        memory = switch_rom_trace_memory();
    }

    FILE *current = fopen(SWITCH_ROM_TRACE_CURRENT, "w");
    if (current != NULL) {
        switch_rom_trace_write_snapshot(
            current,
            failure ? "failure" : (asset_begin ? "asset" : "event"),
            message,
            durable_event ? &memory : NULL);
        fflush(current);
        fclose(current);
    }

    if (durable_event) {
        switch_rom_trace_append(
            SWITCH_ROM_TRACE_EVENTS,
            failure ? "failure" : (asset_begin ? "asset progress" : "event"),
            message,
            &memory);
    }

    if (failure) {
        switch_rom_trace_append(SWITCH_ROM_TRACE_ERRORS, "ROM asset failure", message, &memory);
    }

    if (durable_event) {
        (void)fsdevCommitDevice("sdmc");
    }
}

unsigned int switch_rom_asset_trace_failure_count(void) {
    return sFailureEvents;
}

void switch_rom_asset_trace_exception_snapshot(void) {
    if (!switch_rom_trace_prepare()) {
        return;
    }

    const struct SwitchRomMemorySnapshot memory = switch_rom_trace_memory();
    FILE *file = fopen(SWITCH_ROM_TRACE_EXCEPTION, "w");
    if (file == NULL) {
        return;
    }

    switch_rom_trace_write_snapshot(file, "exception", sLastEvent, &memory);
    fflush(file);
    fclose(file);
    (void)fsdevCommitDevice("sdmc");
}

#endif /* __SWITCH__ */
