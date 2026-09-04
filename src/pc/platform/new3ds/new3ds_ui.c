#include "new3ds_ui.h"

#include "new3ds_log.h"
#include "new3ds_runtime.h"

#include <stdio.h>
#include <string.h>

#ifdef COOPNET
#define NEW3DS_UI_COOPNET_ENABLED 1
#else
#define NEW3DS_UI_COOPNET_ENABLED 0
#endif

#define NEW3DS_TOP_WIDTH 400.0f
#define NEW3DS_BOTTOM_WIDTH 320.0f
#define NEW3DS_SCREEN_HEIGHT 240.0f
#define NEW3DS_MENU_COUNT 4
#define NEW3DS_MENU_Y 42.0f
#define NEW3DS_MENU_STEP 43.0f
#define NEW3DS_MENU_HEIGHT 35.0f

static const char *sMenuTitles[NEW3DS_MENU_COUNT] = {
    "PLAY",
    "MULTIPLAYER",
    "SETTINGS",
    "DIAGNOSTICS",
};

static const char *sMenuSubtitles[NEW3DS_MENU_COUNT] = {
    "Port readiness and launch path",
    "Host / join UX and network status",
    "Handheld performance defaults",
    "Hardware, input and runtime checks",
};

static const char *new3ds_ui_page_title(int page) {
    if (page <= 0 || page > NEW3DS_MENU_COUNT) {
        return "HOME";
    }
    return sMenuTitles[page - 1];
}

static void draw_text(
    C2D_TextBuf buffer,
    const char *text,
    float x,
    float y,
    float scale,
    u32 color,
    bool centered) {
    C2D_Text parsed;
    C2D_TextParse(&parsed, buffer, text);
    C2D_TextOptimize(&parsed);
    C2D_DrawText(
        &parsed,
        C2D_WithColor | (centered ? C2D_AlignCenter : 0),
        x,
        y,
        0.5f,
        scale,
        scale,
        color);
}

static void draw_status_row(
    New3dsUiState *state,
    const char *label,
    const char *value,
    float y,
    u32 value_color) {
    const u32 row_color = C2D_Color32(24, 31, 48, 255);
    const u32 text_color = C2D_Color32(218, 226, 240, 255);

    C2D_DrawRectSolid(32.0f, y, 0.1f, 336.0f, 26.0f, row_color);
    draw_text(state->static_text_buf, label, 43.0f, y + 6.0f, 0.38f, text_color, false);
    draw_text(state->static_text_buf, value, 356.0f, y + 6.0f, 0.34f, value_color, true);
}

static void draw_top(New3dsUiState *state, u64 frame_index) {
    const u32 background = C2D_Color32(9, 12, 20, 255);
    const u32 panel = C2D_Color32(18, 24, 38, 255);
    const u32 white = C2D_Color32(245, 247, 252, 255);
    const u32 muted = C2D_Color32(150, 163, 185, 255);
    const u32 accent = C2D_Color32(239, 70, 73, 255);
    const u32 green = C2D_Color32(75, 205, 142, 255);
    const u32 amber = C2D_Color32(245, 187, 72, 255);
    const u32 dim = C2D_Color32(112, 124, 145, 255);
    const u32 badge = state->is_new_3ds ? green : accent;
    const float pulse = ((frame_index / 30U) & 1U) ? 1.0f : 0.82f;
    char detail[128];

    C2D_TargetClear(state->top_target, background);
    C2D_SceneBegin(state->top_target);
    C2D_TextBufClear(state->static_text_buf);

    C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, NEW3DS_TOP_WIDTH, 8.0f, accent);
    C2D_DrawRectSolid(22.0f, 23.0f, 0.0f, 356.0f, 52.0f, panel);
    C2D_DrawRectSolid(22.0f, 23.0f, 0.1f, 5.0f, 52.0f, accent);

    draw_text(state->static_text_buf, "SM64COOPDX", 38.0f, 33.0f, 0.68f, white, false);
    draw_text(state->static_text_buf, "NEW 3DS PORT", 39.0f, 57.0f, 0.32f, muted, false);

    C2D_DrawRectSolid(267.0f, 36.0f, 0.1f, 94.0f, 24.0f, badge);
    draw_text(
        state->static_text_buf,
        state->is_new_3ds ? "NEW 3DS OK" : "NEW 3DS ONLY",
        314.0f,
        42.0f,
        0.30f,
        C2D_Color32(10, 14, 20, 255),
        true);

    draw_text(state->static_text_buf, "PORT READINESS", 32.0f, 88.0f, 0.35f, muted, false);
    draw_status_row(state, "Runtime + handheld UI", "READY", 105.0f, green);
    draw_status_row(state, "Citro3D game renderer", "NEXT", 134.0f, amber);
    draw_status_row(state, "CoopDX game loop", "PENDING", 163.0f, dim);
    draw_status_row(state, "Coop networking", "PENDING", 192.0f, dim);

    snprintf(
        detail,
        sizeof(detail),
        "%s  |  %s",
        new3ds_ui_page_title(state->active_page),
        state->is_new_3ds ? "804 MHz speedup requested" : "unsupported hardware");
    draw_text(
        state->dynamic_text_buf,
        detail,
        200.0f,
        224.0f,
        0.27f,
        C2D_Color32(245, 247, 252, (u8)(255.0f * pulse)),
        true);
}

static void draw_home(New3dsUiState *state) {
    const u32 white = C2D_Color32(245, 247, 252, 255);
    const u32 muted = C2D_Color32(151, 165, 188, 255);
    const u32 accent = C2D_Color32(239, 70, 73, 255);
    const u32 row = C2D_Color32(22, 29, 44, 255);
    const u32 selected = C2D_Color32(37, 47, 69, 255);

    draw_text(state->static_text_buf, "QUICK MENU", 16.0f, 13.0f, 0.38f, muted, false);

    for (int i = 0; i < NEW3DS_MENU_COUNT; ++i) {
        const float y = NEW3DS_MENU_Y + (float)i * NEW3DS_MENU_STEP;
        const bool is_selected = state->selected_item == i;
        C2D_DrawRectSolid(12.0f, y, 0.0f, 296.0f, NEW3DS_MENU_HEIGHT, is_selected ? selected : row);
        C2D_DrawRectSolid(12.0f, y, 0.1f, is_selected ? 5.0f : 2.0f, NEW3DS_MENU_HEIGHT, is_selected ? accent : muted);
        draw_text(state->static_text_buf, sMenuTitles[i], 25.0f, y + 5.0f, 0.38f, white, false);
        draw_text(state->static_text_buf, sMenuSubtitles[i], 25.0f, y + 20.0f, 0.24f, muted, false);
    }
}

static void draw_page(New3dsUiState *state, u32 keys_held) {
    const u32 white = C2D_Color32(245, 247, 252, 255);
    const u32 muted = C2D_Color32(151, 165, 188, 255);
    const u32 accent = C2D_Color32(239, 70, 73, 255);
    const u32 panel = C2D_Color32(22, 29, 44, 255);
    const u32 green = C2D_Color32(75, 205, 142, 255);

    draw_text(state->static_text_buf, new3ds_ui_page_title(state->active_page), 16.0f, 14.0f, 0.50f, white, false);
    C2D_DrawRectSolid(16.0f, 40.0f, 0.0f, 288.0f, 2.0f, accent);
    C2D_DrawRectSolid(16.0f, 55.0f, 0.0f, 288.0f, 125.0f, panel);

    switch (state->active_page) {
        case 1:
            draw_text(state->dynamic_text_buf, "Foundation build is runnable.", 27.0f, 68.0f, 0.36f, green, false);
            draw_text(state->dynamic_text_buf, "Gameplay is intentionally disabled", 27.0f, 91.0f, 0.30f, white, false);
            draw_text(state->dynamic_text_buf, "until the Citro3D renderer and", 27.0f, 108.0f, 0.30f, white, false);
            draw_text(state->dynamic_text_buf, "CoopDX game loop are linked.", 27.0f, 125.0f, 0.30f, white, false);
            draw_text(state->dynamic_text_buf, "This avoids a fake 'working port'.", 27.0f, 151.0f, 0.27f, muted, false);
            break;
        case 2:
            {
                const bool socReady = new3ds_runtime_network_available();
                char ip[32];
                char logLine[96];
                (void)new3ds_runtime_get_ipv4_string(ip, sizeof(ip));
                draw_text(
                    state->dynamic_text_buf,
                    socReady ? "SOC: READY" : "SOC: OFFLINE",
                    27.0f,
                    68.0f,
                    0.34f,
                    socReady ? green : accent,
                    false);
                snprintf(logLine, sizeof(logLine), "Local IP: %s", ip);
                draw_text(state->dynamic_text_buf, logLine, 27.0f, 90.0f, 0.29f, white, false);
                snprintf(logLine, sizeof(logLine), "Host port: %u", 7777u);
                draw_text(state->dynamic_text_buf, logLine, 27.0f, 112.0f, 0.29f, white, false);
#if NEW3DS_UI_COOPNET_ENABLED
                draw_text(state->dynamic_text_buf, "CoopNet: build enabled", 27.0f, 134.0f, 0.29f, green, false);
#else
                draw_text(state->dynamic_text_buf, "CoopNet: LAN direct only", 27.0f, 134.0f, 0.29f, muted, false);
#endif
                {
                    const uint32_t logCount = new3ds_log_line_count();
                    const uint32_t start = logCount > 3 ? logCount - 3 : 0;
                    float y = 156.0f;
                    for (uint32_t i = start; i < logCount; ++i) {
                        snprintf(logLine, sizeof(logLine), "%s", new3ds_log_line(i));
                        draw_text(state->dynamic_text_buf, logLine, 27.0f, y, 0.22f, muted, false);
                        y += 14.0f;
                    }
                }
            }
            break;
        case 3:
            draw_text(state->dynamic_text_buf, "Handheld defaults", 27.0f, 68.0f, 0.36f, green, false);
            draw_text(state->dynamic_text_buf, "Top: 400x240 game view", 27.0f, 94.0f, 0.29f, white, false);
            draw_text(state->dynamic_text_buf, "Bottom: touch menu / status", 27.0f, 113.0f, 0.29f, white, false);
            draw_text(state->dynamic_text_buf, "Stereoscopic 3D: off by default", 27.0f, 132.0f, 0.29f, white, false);
            draw_text(state->dynamic_text_buf, "Performance target: stable 30 FPS", 27.0f, 151.0f, 0.29f, white, false);
            break;
        case 4:
            {
                char line[128];
                char logSnapshot[256];
                draw_text(
                    state->dynamic_text_buf,
                    state->is_new_3ds ? "Hardware: New 3DS detected" : "Hardware: unsupported model",
                    27.0f,
                    68.0f,
                    0.34f,
                    state->is_new_3ds ? green : accent,
                    false);
                snprintf(line, sizeof(line), "Held keys: 0x%08lX", (unsigned long)keys_held);
                draw_text(state->dynamic_text_buf, line, 27.0f, 90.0f, 0.27f, white, false);
                draw_text(
                    state->dynamic_text_buf,
                    new3ds_runtime_network_available() ? "Network: SOC ready" : "Network: SOC offline",
                    27.0f,
                    108.0f,
                    0.27f,
                    white,
                    false);
                draw_text(state->dynamic_text_buf, "Gfx stats: shell build (N/A)", 27.0f, 126.0f, 0.27f, muted, false);
                logSnapshot[0] = '\0';
                new3ds_log_snapshot(logSnapshot, sizeof(logSnapshot));
                if (logSnapshot[0] != '\0') {
                    draw_text(state->dynamic_text_buf, logSnapshot, 27.0f, 144.0f, 0.20f, muted, false);
                } else {
                    draw_text(state->dynamic_text_buf, "Log buffer empty", 27.0f, 144.0f, 0.24f, muted, false);
                }
            }
            break;
        default:
            break;
    }

    C2D_DrawRectSolid(16.0f, 194.0f, 0.0f, 88.0f, 29.0f, C2D_Color32(37, 47, 69, 255));
    draw_text(state->static_text_buf, "<  B  BACK", 60.0f, 202.0f, 0.30f, white, true);
    draw_text(state->static_text_buf, "L / R: next page", 295.0f, 204.0f, 0.23f, muted, true);
}

static void draw_bottom(New3dsUiState *state, u32 keys_held) {
    const u32 background = C2D_Color32(12, 16, 25, 255);
    const u32 footer = C2D_Color32(18, 24, 38, 255);
    const u32 muted = C2D_Color32(151, 165, 188, 255);

    C2D_TargetClear(state->bottom_target, background);
    C2D_SceneBegin(state->bottom_target);
    C2D_TextBufClear(state->static_text_buf);
    C2D_TextBufClear(state->dynamic_text_buf);

    if (state->active_page == 0) {
        draw_home(state);
    } else {
        draw_page(state, keys_held);
    }

    if (state->active_page == 0) {
        C2D_DrawRectSolid(0.0f, 218.0f, 0.0f, NEW3DS_BOTTOM_WIDTH, 22.0f, footer);
        draw_text(state->static_text_buf, "A / TOUCH  OPEN    START  EXIT", 160.0f, 224.0f, 0.24f, muted, true);
    }
}

bool new3ds_ui_init(
    New3dsUiState *state,
    C3D_RenderTarget *top_target,
    C3D_RenderTarget *bottom_target,
    bool is_new_3ds) {
    if (state == NULL || top_target == NULL || bottom_target == NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->top_target = top_target;
    state->bottom_target = bottom_target;
    state->is_new_3ds = is_new_3ds;
    state->stage = NEW3DS_STAGE_FOUNDATION;
    state->static_text_buf = C2D_TextBufNew(2048);
    state->dynamic_text_buf = C2D_TextBufNew(2048);

    if (state->static_text_buf == NULL || state->dynamic_text_buf == NULL) {
        new3ds_ui_shutdown(state);
        return false;
    }
    return true;
}

void new3ds_ui_shutdown(New3dsUiState *state) {
    if (state == NULL) {
        return;
    }
    if (state->static_text_buf != NULL) {
        C2D_TextBufDelete(state->static_text_buf);
        state->static_text_buf = NULL;
    }
    if (state->dynamic_text_buf != NULL) {
        C2D_TextBufDelete(state->dynamic_text_buf);
        state->dynamic_text_buf = NULL;
    }
}

void new3ds_ui_handle_input(
    New3dsUiState *state,
    u32 keys_down,
    u32 keys_held,
    const touchPosition *touch) {
    if (state == NULL) {
        return;
    }

    if (state->active_page == 0) {
        if ((keys_down & KEY_DUP) != 0) {
            state->selected_item = (state->selected_item + NEW3DS_MENU_COUNT - 1) % NEW3DS_MENU_COUNT;
        }
        if ((keys_down & KEY_DDOWN) != 0) {
            state->selected_item = (state->selected_item + 1) % NEW3DS_MENU_COUNT;
        }
        if ((keys_down & KEY_A) != 0) {
            state->active_page = state->selected_item + 1;
        }
    } else {
        if ((keys_down & KEY_B) != 0) {
            state->selected_item = state->active_page - 1;
            state->active_page = 0;
        } else if ((keys_down & KEY_L) != 0) {
            state->active_page--;
            if (state->active_page < 1) {
                state->active_page = NEW3DS_MENU_COUNT;
            }
            state->selected_item = state->active_page - 1;
        } else if ((keys_down & KEY_R) != 0) {
            state->active_page++;
            if (state->active_page > NEW3DS_MENU_COUNT) {
                state->active_page = 1;
            }
            state->selected_item = state->active_page - 1;
        }
    }

    const bool touch_active = (keys_held & KEY_TOUCH) != 0;
    if (touch_active && !state->touch_down_last_frame && touch != NULL) {
        if (state->active_page == 0) {
            for (int i = 0; i < NEW3DS_MENU_COUNT; ++i) {
                const int top = (int)(NEW3DS_MENU_Y + (float)i * NEW3DS_MENU_STEP);
                const int bottom = top + (int)NEW3DS_MENU_HEIGHT;
                if (touch->px >= 12 && touch->px <= 308 && touch->py >= top && touch->py <= bottom) {
                    state->selected_item = i;
                    state->active_page = i + 1;
                    break;
                }
            }
        } else if (touch->px >= 16 && touch->px <= 104 && touch->py >= 194 && touch->py <= 226) {
            state->selected_item = state->active_page - 1;
            state->active_page = 0;
        }
    }
    state->touch_down_last_frame = touch_active;
}

void new3ds_ui_draw(New3dsUiState *state, u32 keys_held, u64 frame_index) {
    if (state == NULL) {
        return;
    }
    C2D_TextBufClear(state->dynamic_text_buf);
    draw_top(state, frame_index);
    draw_bottom(state, keys_held);
}
