#ifdef __3DS__

#include "new3ds_bottom_ui.h"

#include <3ds.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pc/configfile.h"
#include "pc/platform/new3ds/new3ds_log.h"

/*
 * Bottom screen is 320x240 landscape (framebuffer is portrait 240x320).
 * PrintConsole owns this screen. Do not also printf() into it from the logger
 * while this UI is active — that fights the redraw and corrupts the bottom rows.
 */
#define NEW3DS_LOG_HEADER_ROWS 2
/* Leave one blank row so PrintConsole never auto-scrolls mid-redraw. */
#define NEW3DS_LOG_BOTTOM_MARGIN 1

static bool sReady = false;
static PrintConsole sConsole;
static bool sConsoleInited = false;
static bool sPaused = false;
static bool sClearedWhileDisabled = false;
static uint32_t sDrawnLogCount = UINT32_MAX;
static uint32_t sDrawnWriteIndex = UINT32_MAX;

bool new3ds_bottom_ui_init(void) {
    if (!sConsoleInited) {
        consoleInit(GFX_BOTTOM, &sConsole);
        sConsoleInited = true;
    }
    consoleSelect(&sConsole);
    consoleSetWindow(&sConsole, 0, 0, sConsole.consoleWidth, sConsole.consoleHeight);
    consoleClear();
    sReady = true;
    sPaused = false;
    sDrawnLogCount = UINT32_MAX;
    sDrawnWriteIndex = UINT32_MAX;
    return true;
}

void new3ds_bottom_ui_shutdown(void) {
    sReady = false;
    sPaused = false;
}

bool new3ds_bottom_ui_ready(void) {
    return sReady;
}

bool new3ds_bottom_ui_owns_console(void) {
    return sReady && sConsoleInited && !sPaused;
}

void new3ds_bottom_ui_set_paused(bool paused) {
    sPaused = paused;
    if (!paused) {
        sDrawnLogCount = UINT32_MAX;
        sDrawnWriteIndex = UINT32_MAX;
    }
}

void new3ds_bottom_ui_reinit_console(void) {
    consoleInit(GFX_BOTTOM, &sConsole);
    sConsoleInited = true;
    consoleSelect(&sConsole);
    consoleSetWindow(&sConsole, 0, 0, sConsole.consoleWidth, sConsole.consoleHeight);
    consoleClear();
    sDrawnLogCount = UINT32_MAX;
    sDrawnWriteIndex = UINT32_MAX;
}

bool new3ds_bottom_ui_touch_consumed(void) {
    return false;
}

void new3ds_bottom_ui_poll_input(void) {
}

void new3ds_bottom_ui_draw(void) {
    if (!sReady || !sConsoleInited || sPaused) {
        return;
    }

    if (!configNew3dsLogs) {
        if (!sClearedWhileDisabled) {
            consoleSelect(&sConsole);
            printf("\x1b[2J\x1b[H");
            fflush(stdout);
            gfxFlushBuffers();
            sClearedWhileDisabled = true;
            sDrawnLogCount = UINT32_MAX;
            sDrawnWriteIndex = UINT32_MAX;
        }
        return;
    }
    sClearedWhileDisabled = false;

    const uint32_t count = new3ds_log_line_count();
    const uint32_t writeIndex = new3ds_log_write_index();

    /* Skip redraw when nothing changed — full clear every frame glitches the bottom. */
    if (count == sDrawnLogCount && writeIndex == sDrawnWriteIndex) {
        return;
    }
    sDrawnLogCount = count;
    sDrawnWriteIndex = writeIndex;

    consoleSelect(&sConsole);

    const int cols = sConsole.consoleWidth > 0 ? sConsole.consoleWidth : 40;
    const int rows = sConsole.consoleHeight > 0 ? sConsole.consoleHeight : 30;
    int visible = rows - NEW3DS_LOG_HEADER_ROWS - NEW3DS_LOG_BOTTOM_MARGIN;
    if (visible < 1) {
        visible = 1;
    }
    /* Keep one column free so PrintConsole never wraps a line onto the next row. */
    const int maxCols = cols > 1 ? cols - 1 : cols;
    char clipped[64];
    const size_t clipCap = ((size_t)maxCols < sizeof(clipped) - 1)
        ? (size_t)maxCols
        : sizeof(clipped) - 1;

    printf("\x1b[2J\x1b[H");
    printf(CONSOLE_CYAN "LOG" CONSOLE_RESET " (latest)\n");
    printf("--------------------------------------\n");

    if (count == 0) {
        printf(CONSOLE_WHITE "(no log lines yet)\n" CONSOLE_RESET);
        fflush(stdout);
        gfxFlushBuffers();
        return;
    }

    /* Always pin to the newest lines. */
    uint32_t start = 0;
    if (count > (uint32_t)visible) {
        start = count - (uint32_t)visible;
    }

    uint32_t printed = 0;
    for (uint32_t i = start; i < count && printed < (uint32_t)visible; ++i, ++printed) {
        const char *line = new3ds_log_line(i);
        size_t len = 0;
        while (line[len] != '\0' && len < clipCap) {
            clipped[len] = line[len];
            len++;
        }
        clipped[len] = '\0';

        const bool is_error = (line[0] == '[' && line[1] == 'E');
        const bool is_warn = (line[0] == '[' && line[1] == 'W');
        if (is_error) {
            printf(CONSOLE_RED "%s\n" CONSOLE_RESET, clipped);
        } else if (is_warn) {
            printf(CONSOLE_YELLOW "%s\n" CONSOLE_RESET, clipped);
        } else {
            printf(CONSOLE_WHITE "%s\n" CONSOLE_RESET, clipped);
        }
    }

    fflush(stdout);
    gfxFlushBuffers();
}

#endif /* __3DS__ */
