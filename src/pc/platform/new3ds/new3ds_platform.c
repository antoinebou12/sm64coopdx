#ifdef __3DS__

#include <3ds.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pc/platform.h"

#define NEW3DS_DATA_PARENT "sdmc:/3ds"
#define NEW3DS_DATA_ROOT "sdmc:/3ds/sm64coopdx"
#define NEW3DS_EXECUTABLE NEW3DS_DATA_ROOT "/sm64coopdx.3dsx"

static void new3ds_ensure_data_root(void) {
    if (mkdir(NEW3DS_DATA_PARENT, 0777) != 0 && errno != EEXIST) return;
    (void)mkdir(NEW3DS_DATA_ROOT, 0777);
}

char *sys_strlwr(char *src) {
    if (src == NULL) return NULL;
    for (unsigned char *p = (unsigned char *)src; *p; ++p) {
        *p = (unsigned char)tolower(*p);
    }
    return src;
}

char *sys_strdup(const char *src) {
    if (src == NULL) return NULL;
    const size_t len = strlen(src) + 1;
    char *copy = (char *)malloc(len);
    if (copy != NULL) memcpy(copy, src, len);
    return copy;
}

int sys_strcasecmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    if (p1 == p2) return 0;

    while (*p1 != '\0' && *p2 != '\0') {
        const int diff = tolower(*p1) - tolower(*p2);
        if (diff != 0) return diff;
        ++p1;
        ++p2;
    }
    return tolower(*p1) - tolower(*p2);
}

const char *sys_file_name(const char *fpath) {
    if (fpath == NULL) return NULL;
    const char *sep1 = strrchr(fpath, '/');
    const char *sep2 = strrchr(fpath, '\\');
    const char *sep = sep1 > sep2 ? sep1 : sep2;
    return sep != NULL ? sep + 1 : fpath;
}

const char *sys_file_extension(const char *fpath) {
    const char *name = sys_file_name(fpath);
    if (name == NULL) return NULL;
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot == name || dot[1] == '\0') return NULL;
    return dot + 1;
}

void sys_swap_backslashes(char *buffer) {
    if (buffer == NULL) return;
    bool in_color = false;
    for (size_t i = 0; buffer[i] != '\0'; ++i) {
        if (buffer[i] == '\\' && buffer[i + 1] == '#') {
            in_color = true;
            continue;
        }
        if (buffer[i] == '\\' && !in_color) {
            buffer[i] = '/';
        } else if (buffer[i] == '\\' && in_color && buffer[i + 1] != '#') {
            in_color = false;
        }
    }
}

const char *sys_user_path(void) {
    new3ds_ensure_data_root();
    return NEW3DS_DATA_ROOT;
}

const char *sys_resource_path(void) {
    new3ds_ensure_data_root();
    return NEW3DS_DATA_ROOT;
}

const char *sys_exe_path_dir(void) {
    new3ds_ensure_data_root();
    return NEW3DS_DATA_ROOT;
}

const char *sys_exe_path_file(void) {
    return NEW3DS_EXECUTABLE;
}

void sys_fatal(const char *fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    fprintf(stderr, "FATAL ERROR: %s\n", message);
    fflush(stderr);

    /*
     * svcBreak gives hbmenu/Luma a useful crash surface even if graphics are
     * not initialized far enough to display an in-game dialog.
     */
    svcBreak(USERBREAK_PANIC);
    exit(1);
}

#endif /* __3DS__ */
