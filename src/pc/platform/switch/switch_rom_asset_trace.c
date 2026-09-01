#ifdef __SWITCH__

#include "switch_rom_asset_trace.h"

#include <switch.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#define SWITCH_ROM_TRACE_ROOT "sdmc:/switch/sm64coopdx"
#define SWITCH_ROM_TRACE_LOG_DIR SWITCH_ROM_TRACE_ROOT "/logs"
#define SWITCH_ROM_TRACE_CURRENT SWITCH_ROM_TRACE_LOG_DIR "/current_rom_asset.txt"

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

void switch_rom_asset_trace_printf(const char *fmt, ...) {
#ifdef SWITCH_NO_LOGS
    (void)fmt;
    return;
#endif
    if (fmt == NULL || !switch_rom_trace_prepare()) {
        return;
    }

    FILE *file = fopen(SWITCH_ROM_TRACE_CURRENT, "w");
    if (file == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    fputc('\n', file);
    fflush(file);
    fclose(file);
    (void)fsdevCommitDevice("sdmc");
}

#endif /* __SWITCH__ */
