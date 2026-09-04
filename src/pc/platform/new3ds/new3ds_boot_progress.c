#ifdef __3DS__

#include "new3ds_boot_progress.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "pc/platform/new3ds/new3ds_log.h"

static bool sActive = false;
static bool sGfxOwned = false;
static char sLastPhase[96];

bool new3ds_boot_progress_active(void) {
    return sActive;
}

void new3ds_boot_progress_pump(void) {
    if (!aptMainLoop()) {
        return;
    }
    hidScanInput();
    gspWaitForVBlank();
}

void new3ds_boot_progress_begin(void) {
    if (sActive) {
        return;
    }

    if (!sGfxOwned) {
        gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);
        gfxSet3D(false);
        sGfxOwned = true;
    }

    consoleInit(GFX_BOTTOM, NULL);
    printf("\x1b[2J\x1b[1;1H");
    printf("SM64CoopDX New 3DS\n");
    printf("--------------------\n");
    printf("Booting...\n");
    fflush(stdout);

    sLastPhase[0] = '\0';
    sActive = true;
    new3ds_boot_progress_pump();
    new3ds_boot_progress_pump();

    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "boot", "bottom progress console ready");
}

void new3ds_boot_progress_set(const char *phase) {
    if (phase == NULL) {
        phase = "";
    }

    if (!sActive) {
        new3ds_boot_progress_begin();
    }

    if (strncmp(sLastPhase, phase, sizeof(sLastPhase) - 1) == 0) {
        new3ds_boot_progress_pump();
        return;
    }

    snprintf(sLastPhase, sizeof(sLastPhase), "%s", phase);
    printf("\x1b[5;1H\x1b[2K%s\n", phase);
    fflush(stdout);
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "boot", "%s", phase);
    new3ds_boot_progress_pump();
}

void new3ds_boot_progress_end(void) {
    if (!sActive) {
        return;
    }
    printf("\x1b[5;1H\x1b[2KReady.\n");
    fflush(stdout);
    new3ds_boot_progress_pump();
    sActive = false;
    /* Leave gfxInit for gfx_wm / Citro3D. Bottom console is retaken by log UI. */
}

bool new3ds_boot_progress_gfx_started(void) {
    return sGfxOwned;
}

#endif /* __3DS__ */
