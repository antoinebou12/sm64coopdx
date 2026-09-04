#pragma once

#ifdef __3DS__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef NEW3DS_LOG_LEVEL
#define NEW3DS_LOG_LEVEL 2
#endif

#define NEW3DS_LOG_LINE_COUNT 32
#define NEW3DS_LOG_LINE_SIZE  160

typedef enum New3dsLogCategory {
    NEW3DS_LOG_CAT_RUNTIME = 0,
    NEW3DS_LOG_CAT_NET,
    NEW3DS_LOG_CAT_GFX,
    NEW3DS_LOG_CAT_PERF,
    NEW3DS_LOG_CAT_COOPNET,
} New3dsLogCategory;

void new3ds_log_write(const char *level, const char *tag, const char *fmt, ...);
void new3ds_log_init(void);
void new3ds_log_flush(void);
void new3ds_log_shutdown(void);
const char *new3ds_log_directory(void);
void new3ds_log_snapshot(char *buf, size_t len);
uint32_t new3ds_log_line_count(void);
uint32_t new3ds_log_write_index(void);
const char *new3ds_log_line(uint32_t index);
bool new3ds_log_category_enabled(New3dsLogCategory category);
int new3ds_log_level_value(const char *level);

#define NEW3DS_LOG_ENABLED(level) (NEW3DS_LOG_LEVEL > 0 && new3ds_log_level_value(level) <= NEW3DS_LOG_LEVEL)

#define NEW3DS_LOG_CAT(level, cat, tag, ...) \
    do { \
        if (NEW3DS_LOG_ENABLED(level) && new3ds_log_category_enabled(cat)) { \
            new3ds_log_write(level, tag, __VA_ARGS__); \
        } \
    } while (0)

#define NEW3DS_LOG_INFO(tag, ...)  NEW3DS_LOG_CAT("INFO", NEW3DS_LOG_CAT_RUNTIME, tag, __VA_ARGS__)
#define NEW3DS_LOG_WARN(tag, ...)  NEW3DS_LOG_CAT("WARN", NEW3DS_LOG_CAT_RUNTIME, tag, __VA_ARGS__)
#define NEW3DS_LOG_ERROR(tag, ...) \
    do { \
        if (NEW3DS_LOG_ENABLED("ERROR")) { \
            new3ds_log_write("ERROR", tag, __VA_ARGS__); \
        } \
    } while (0)

#define NEW3DS_LOG_INFO_CAT(cat, tag, ...)  NEW3DS_LOG_CAT("INFO", cat, tag, __VA_ARGS__)
#define NEW3DS_LOG_WARN_CAT(cat, tag, ...)  NEW3DS_LOG_CAT("WARN", cat, tag, __VA_ARGS__)
#define NEW3DS_LOG_ERROR_CAT(cat, tag, ...) \
    do { \
        if (NEW3DS_LOG_ENABLED("ERROR") && new3ds_log_category_enabled(cat)) { \
            new3ds_log_write("ERROR", tag, __VA_ARGS__); \
        } \
    } while (0)

#define NEW3DS_LOG_VERBOSE_CAT(cat, tag, ...) NEW3DS_LOG_CAT("VERBOSE", cat, tag, __VA_ARGS__)

#endif /* __3DS__ */
