#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdbool.h>
#include <stdio.h>

#include "new3ds_ui.h"

static void new3ds_platform_shutdown_graphics(New3dsUiState *ui) {
    new3ds_ui_shutdown(ui);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

int main(void) {
    bool is_new_3ds = false;
    u64 frame_index = 0;
    New3dsUiState ui = {0};

    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);

    if (R_SUCCEEDED(APT_CheckNew3DS(&is_new_3ds)) && is_new_3ds) {
        osSetSpeedupEnable(true);
    }

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!new3ds_ui_init(&ui, top_target, bottom_target, is_new_3ds)) {
        consoleInit(GFX_BOTTOM, NULL);
        printf("SM64CoopDX New 3DS\n\nUI initialization failed.\nPress START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if ((hidKeysDown() & KEY_START) != 0) {
                break;
            }
            gspWaitForVBlank();
        }
        new3ds_platform_shutdown_graphics(&ui);
        return 1;
    }

    while (aptMainLoop()) {
        touchPosition touch = {0};
        hidScanInput();
        const u32 keys_down = hidKeysDown();
        const u32 keys_held = hidKeysHeld();

        if ((keys_down & KEY_START) != 0) {
            break;
        }
        if ((keys_held & KEY_TOUCH) != 0) {
            hidTouchRead(&touch);
        }

        new3ds_ui_handle_input(&ui, keys_down, keys_held, &touch);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        new3ds_ui_draw(&ui, keys_held, frame_index++);
        C3D_FrameEnd(0);
    }

    new3ds_platform_shutdown_graphics(&ui);
    return 0;
}
