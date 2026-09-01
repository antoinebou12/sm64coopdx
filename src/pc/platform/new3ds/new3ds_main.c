#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>

#include "new3ds_runtime.h"
#include "new3ds_ui.h"

static void new3ds_platform_shutdown_graphics(New3dsUiState *ui) {
    new3ds_ui_shutdown(ui);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

int main(void) {
    New3dsRuntimeState runtime = {0};
    New3dsUiState ui = {0};

    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);

    if (!new3ds_runtime_init(&runtime)) {
        consoleInit(GFX_BOTTOM, NULL);
        printf("SM64CoopDX New 3DS\n\nRuntime initialization failed.\n");
        gfxExit();
        return 1;
    }

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!new3ds_ui_init(&ui, top_target, bottom_target, runtime.is_new_3ds)) {
        consoleInit(GFX_BOTTOM, NULL);
        printf("SM64CoopDX New 3DS\n\nUI initialization failed.\nPress START to exit.\n");
        while (new3ds_runtime_poll(&runtime)) {
            if ((runtime.input.down & KEY_START) != 0) {
                break;
            }
            gspWaitForVBlank();
        }
        new3ds_platform_shutdown_graphics(&ui);
        new3ds_runtime_shutdown(&runtime);
        return 1;
    }

    while (new3ds_runtime_poll(&runtime)) {
        if ((runtime.input.down & KEY_START) != 0) {
            new3ds_runtime_request_exit(&runtime);
            break;
        }

        new3ds_ui_handle_input(
            &ui,
            runtime.input.down,
            runtime.input.held,
            &runtime.input.touch);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        new3ds_ui_draw(&ui, runtime.input.held, runtime.frame_index);
        C3D_FrameEnd(0);
    }

    new3ds_platform_shutdown_graphics(&ui);
    new3ds_runtime_shutdown(&runtime);
    return 0;
}
