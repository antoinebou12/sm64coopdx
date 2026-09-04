#ifdef __3DS__

#include "new3ds_log.h"

#ifndef NEW3DS_SHELL_BUILD
#include "pc/configfile.h"
#else
static bool configNew3dsLogs = false;
static bool configNew3dsLogNet = true;
static bool configNew3dsLogGfx = false;
static bool configNew3dsLogPerf = false;
static bool configNew3dsLogCoopnet = false;
static bool configDebugInfo = false;
static bool configShowFPS = false;
#endif

#include <3ds.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifndef NEW3DS_SHELL_BUILD
#include "pc/platform/new3ds/new3ds_bottom_ui.h"
#endif

#define NEW3DS_LOG_ROOT "sdmc:/3ds/sm64coopdx"
#define NEW3DS_LOG_DIR NEW3DS_LOG_ROOT "/logs"
#define NEW3DS_RUNTIME_LOG NEW3DS_LOG_DIR "/runtime.log"
#define NEW3DS_LOG_FLUSH_INTERVAL 16u

static char sLogLines[NEW3DS_LOG_LINE_COUNT][NEW3DS_LOG_LINE_SIZE];
static uint32_t sLogWriteIndex = 0;
static uint32_t sLogCount = 0;
static bool sFileLogReady = false;
static uint32_t sPendingFileWrites = 0;

static bool new3ds_log_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool new3ds_log_prepare_file(void) {
    if (sFileLogReady) {
        return true;
    }

    if (!new3ds_log_mkdir("sdmc:/3ds")) {
        return false;
    }
    if (!new3ds_log_mkdir(NEW3DS_LOG_ROOT)) {
        return false;
    }
    if (!new3ds_log_mkdir(NEW3DS_LOG_DIR)) {
        return false;
    }

    sFileLogReady = true;
    return true;
}

static void new3ds_log_commit_sdmc(void) {
    sPendingFileWrites = 0;
}

static void new3ds_log_append_file(const char *line, bool force_commit) {
    if (line == NULL || !new3ds_log_prepare_file()) {
        return;
    }

    FILE *file = fopen(NEW3DS_RUNTIME_LOG, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "[%lld] %s\n", (long long)time(NULL), line);
    fclose(file);

    sPendingFileWrites++;
    if (force_commit || sPendingFileWrites >= NEW3DS_LOG_FLUSH_INTERVAL) {
        new3ds_log_commit_sdmc();
    }
}

const char *new3ds_log_directory(void) {
    return NEW3DS_LOG_DIR;
}

/*
 * Errors and boot checkpoints are written even when file logging is off. The
 * DJUI checkbox that turns logging on is unreachable when the failure is
 * gfx_init itself, so gating these left a dead console with no diagnostic
 * anywhere. Everything else still respects configNew3dsLogs.
 */
static bool new3ds_log_is_critical(const char *level, const char *tag) {
    if (level != NULL && strcmp(level, "ERROR") == 0) {
        return true;
    }
    return tag != NULL && strcmp(tag, "boot") == 0;
}

void new3ds_log_init(void) {
    if (!new3ds_log_prepare_file()) {
        return;
    }

    FILE *file = fopen(NEW3DS_RUNTIME_LOG, "a");
    if (file != NULL) {
        fprintf(file, "\n=== SM64CoopDX session begin epoch=%lld ===\n", (long long)time(NULL));
        fclose(file);
        new3ds_log_commit_sdmc();
    }
}

void new3ds_log_flush(void) {
    if (sFileLogReady && sPendingFileWrites > 0) {
        new3ds_log_commit_sdmc();
    }
}

void new3ds_log_shutdown(void) {
    if (!sFileLogReady) {
        return;
    }

    FILE *file = fopen(NEW3DS_RUNTIME_LOG, "a");
    if (file != NULL) {
        fprintf(file, "=== SM64CoopDX session end epoch=%lld ===\n", (long long)time(NULL));
        fclose(file);
    }

    new3ds_log_commit_sdmc();
    sFileLogReady = false;
}

int new3ds_log_level_value(const char *level) {
    if (level == NULL) {
        return 2;
    }
    if (strcmp(level, "ERROR") == 0) {
        return 1;
    }
    if (strcmp(level, "WARN") == 0) {
        return 2;
    }
    if (strcmp(level, "VERBOSE") == 0) {
        return 3;
    }
    return 2;
}

bool new3ds_log_category_enabled(New3dsLogCategory category) {
    switch (category) {
        case NEW3DS_LOG_CAT_RUNTIME:
            return true;
        case NEW3DS_LOG_CAT_NET:
            return configNew3dsLogNet || configDebugInfo;
        case NEW3DS_LOG_CAT_GFX:
            return configNew3dsLogGfx;
        case NEW3DS_LOG_CAT_PERF:
            return configNew3dsLogPerf || configShowFPS;
        case NEW3DS_LOG_CAT_COOPNET:
            return configNew3dsLogCoopnet;
    }
    return false;
}

void new3ds_log_write(const char *level, const char *tag, const char *fmt, ...) {
#if NEW3DS_LOG_LEVEL == 0
    (void)level;
    (void)tag;
    (void)fmt;
    return;
#else
    /*
     * Must track the emitted line, not a smaller fixed cap: at 64 the perf
     * line lost "degraded=/dropped=" and messages ended mid-word.
     */
    char message[NEW3DS_LOG_LINE_SIZE];
    char line[NEW3DS_LOG_LINE_SIZE];
    va_list args;
    bool is_error;

    if (level == NULL) {
        level = "INFO";
    }
    if (tag == NULL) {
        tag = "new3ds";
    }
    if (!configNew3dsLogs && !new3ds_log_is_critical(level, tag)) {
        return;
    }
    if (new3ds_log_level_value(level) > NEW3DS_LOG_LEVEL) {
        return;
    }

    is_error = strcmp(level, "ERROR") == 0;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt ? fmt : "", args);
    va_end(args);

    snprintf(line, sizeof(line), "[%s][%s] %s", level, tag, message);
    line[sizeof(line) - 1] = '\0';

    /*
     * When the bottom-screen log UI owns PrintConsole, do not printf here —
     * that races the UI redraw and corrupts the last rows. Early boot (before
     * bottom UI) still prints so Homebrew Launcher progress stays visible.
     */
#ifndef NEW3DS_SHELL_BUILD
    if (!new3ds_bottom_ui_owns_console()) {
        printf("%s\n", line);
    }
#else
    printf("%s\n", line);
#endif
    svcOutputDebugString(line, (s32)strlen(line));
    new3ds_log_append_file(line, is_error);

    {
        const size_t copy_len = strnlen(line, NEW3DS_LOG_LINE_SIZE - 1);
        memcpy(sLogLines[sLogWriteIndex], line, copy_len);
        sLogLines[sLogWriteIndex][copy_len] = '\0';
    }
    sLogWriteIndex = (sLogWriteIndex + 1) % NEW3DS_LOG_LINE_COUNT;
    if (sLogCount < NEW3DS_LOG_LINE_COUNT) {
        sLogCount++;
    }
#endif
}

uint32_t new3ds_log_line_count(void) {
    return sLogCount;
}

uint32_t new3ds_log_write_index(void) {
    return sLogWriteIndex;
}

const char *new3ds_log_line(uint32_t index) {
    if (index >= sLogCount) {
        return "";
    }

    uint32_t start = (sLogWriteIndex + NEW3DS_LOG_LINE_COUNT - sLogCount) % NEW3DS_LOG_LINE_COUNT;
    uint32_t line_index = (start + index) % NEW3DS_LOG_LINE_COUNT;
    return sLogLines[line_index];
}

void new3ds_log_snapshot(char *buf, size_t len) {
    if (buf == NULL || len == 0) {
        return;
    }

    buf[0] = '\0';
    for (uint32_t i = 0; i < sLogCount; ++i) {
        const char *line = new3ds_log_line(i);
        if (buf[0] != '\0') {
            strncat(buf, "\n", len - strlen(buf) - 1);
        }
        strncat(buf, line, len - strlen(buf) - 1);
    }
}

#endif /* __3DS__ */
