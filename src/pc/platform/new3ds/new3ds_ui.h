#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

typedef enum New3dsPortStage {
    NEW3DS_STAGE_FOUNDATION = 0,
    NEW3DS_STAGE_RENDERER,
    NEW3DS_STAGE_GAME_CORE,
    NEW3DS_STAGE_NETWORK,
} New3dsPortStage;

typedef struct New3dsUiState {
    C3D_RenderTarget *top_target;
    C3D_RenderTarget *bottom_target;
    C2D_TextBuf static_text_buf;
    C2D_TextBuf dynamic_text_buf;
    int selected_item;
    int active_page;
    bool is_new_3ds;
    bool touch_down_last_frame;
    New3dsPortStage stage;
} New3dsUiState;

bool new3ds_ui_init(
    New3dsUiState *state,
    C3D_RenderTarget *top_target,
    C3D_RenderTarget *bottom_target,
    bool is_new_3ds);
void new3ds_ui_shutdown(New3dsUiState *state);
void new3ds_ui_handle_input(
    New3dsUiState *state,
    u32 keys_down,
    u32 keys_held,
    const touchPosition *touch);
void new3ds_ui_draw(New3dsUiState *state, u32 keys_held, u64 frame_index);
