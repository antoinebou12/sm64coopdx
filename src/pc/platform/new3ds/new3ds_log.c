#ifdef __3DS__

#include "new3ds_log.h"

#ifndef NEW3DS_SHELL_BUILD
#include "pc/configfile.h"
#else
static bool configNew3dsLogNet = true;
static bool configNew3dsLogGfx = false;
static bool configNew3dsLogPerf = false;
static bool configNew3dsLogCoopnet = false;
static bool configDebugInfo = false;
static bool configShowFPS = false;
#endif

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char sLogLines[NEW3DS_LOG_LINE_COUNT][NEW3DS_LOG_LINE_SIZE];
static uint32_t sLogWriteIndex = 0;
static uint32_t sLogCount = 0;

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
    char message[64];
    char line[NEW3DS_LOG_LINE_SIZE];
    va_list args;

    if (level == NULL) {
        level = "INFO";
    }
    if (tag == NULL) {
        tag = "new3ds";
    }
    if (new3ds_log_level_value(level) > NEW3DS_LOG_LEVEL) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt ? fmt : "", args);
    va_end(args);

    snprintf(line, sizeof(line), "[%s][%s] %s", level, tag, message);
    line[sizeof(line) - 1] = '\0';

    printf("%s\n", line);
    svcOutputDebugString(line, (s32)strlen(line));

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
