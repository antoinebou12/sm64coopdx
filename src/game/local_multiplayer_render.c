#include "local_multiplayer_render.h"

#include <string.h>

#include "area.h"
#include "camera.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "gfx_dimensions.h"
#include "local_multiplayer.h"
#include "mario.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"

static s16 sLocalCameraYaw[LOCAL_MULTIPLAYER_MAX_PLAYERS] = { 0 };
static u32 sLastCameraInputFrame = 0xFFFFFFFFu;

static bool local_multiplayer_can_render_multiple(Vp *viewport_override, Vp *viewport_clip) {
    if (local_multiplayer_player_count() <= 1) return false;
    if (viewport_override != NULL || viewport_clip != NULL) return false;
    if (gCurrentArea == NULL || gCurrentArea->camera == NULL) return false;
    if (gMarioObjects[0] == NULL) return false;
    return true;
}

static void local_multiplayer_update_camera_input(void) {
    if (sLastCameraInputFrame == gGlobalTimer) return;
    sLastCameraInputFrame = gGlobalTimer;

    const u8 count = local_multiplayer_player_count();
    for (u8 player = 1; player < count; player++) {
        if (!local_multiplayer_is_active_player(player)) continue;

        s16 axis = gControllers[player].extStickX;
        if (axis > -12 && axis < 12) continue;

        /* Roughly 180 degrees/second at full deflection at the native 30 Hz tick. */
        sLocalCameraYaw[player] -= axis * 8;
    }
}

static void translate_camera_vec(Vec3f value, const Vec3f delta) {
    value[0] += delta[0];
    value[1] += delta[1];
    value[2] += delta[2];
}

static void rotate_camera_vec_about_focus(Vec3f value, const Vec3f focus, s16 yaw) {
    const f32 dx = value[0] - focus[0];
    const f32 dz = value[2] - focus[2];
    const f32 s = sins(yaw);
    const f32 c = coss(yaw);

    value[0] = focus[0] + dx * c + dz * s;
    value[2] = focus[2] - dx * s + dz * c;
}

static void local_multiplayer_apply_camera(u8 player, const struct LakituState *base) {
    gLakituState = *base;
    if (player == 0 || player >= MAX_PLAYERS || gMarioObjects[player] == NULL) return;

    Vec3f delta = {
        gMarioStates[player].pos[0] - gMarioStates[0].pos[0],
        gMarioStates[player].pos[1] - gMarioStates[0].pos[1],
        gMarioStates[player].pos[2] - gMarioStates[0].pos[2],
    };

    translate_camera_vec(gLakituState.curFocus, delta);
    translate_camera_vec(gLakituState.curPos, delta);
    translate_camera_vec(gLakituState.goalFocus, delta);
    translate_camera_vec(gLakituState.goalPos, delta);
    translate_camera_vec(gLakituState.focus, delta);
    translate_camera_vec(gLakituState.pos, delta);

    const s16 yaw = sLocalCameraYaw[player];
    rotate_camera_vec_about_focus(gLakituState.curPos, gLakituState.curFocus, yaw);
    rotate_camera_vec_about_focus(gLakituState.goalPos, gLakituState.goalFocus, yaw);
    rotate_camera_vec_about_focus(gLakituState.pos, gLakituState.focus, yaw);

    gLakituState.yaw += yaw;
    gLakituState.nextYaw += yaw;
}

static bool local_multiplayer_make_viewport(u8 player, Vp *viewport) {
    LocalViewportPixels pixels;
    if (viewport == NULL ||
        !local_multiplayer_get_viewport_pixels(player, SCREEN_WIDTH, SCREEN_HEIGHT, &pixels)) {
        return false;
    }

    memset(viewport, 0, sizeof(*viewport));
    viewport->vp.vscale[0] = pixels.width * 2;
    viewport->vp.vscale[1] = pixels.height * 2;
    viewport->vp.vscale[2] = 511;

    viewport->vp.vtrans[0] = pixels.x * 4 + pixels.width * 2;
    viewport->vp.vtrans[1] = SCREEN_HEIGHT * 4 - pixels.y * 4 - pixels.height * 2;
    viewport->vp.vtrans[2] = 511;
    return true;
}

void local_multiplayer_render_scene(struct GraphNodeRoot *root, Vp *viewport_override,
                                    Vp *viewport_clip, s32 clear_color) {
    if (root == NULL) return;

    if (!local_multiplayer_can_render_multiple(viewport_override, viewport_clip)) {
        local_multiplayer_begin_player(0);
        geo_process_root(root, viewport_override, viewport_clip, clear_color);
        local_multiplayer_end_player();
        return;
    }

    local_multiplayer_update_camera_input();

    const struct LakituState base_camera = gLakituState;
    const u8 count = local_multiplayer_player_count();

    for (u8 player = 0; player < count; player++) {
        if (!local_multiplayer_is_active_player(player) || gMarioObjects[player] == NULL) continue;

        Vp viewport;
        if (!local_multiplayer_make_viewport(player, &viewport)) continue;

        local_multiplayer_apply_camera(player, &base_camera);
        local_multiplayer_begin_player(player);
        geo_process_root(root, &viewport, NULL, clear_color);
        local_multiplayer_end_player();
    }

    gLakituState = base_camera;
}
