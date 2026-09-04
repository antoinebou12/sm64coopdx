#ifdef __3DS__

#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>
#include <stddef.h>

#include "pc/gfx/gfx_citro3d_new3ds.h"
#include "pc/gfx/gfx_rendering_api.h"

/*
 * Hardware state guard for the Citro3D backend.
 *
 * The renderer caches the requested depth/blend state to avoid redundant PICA
 * commands. Its fog pass temporarily changes C3D_DepthTest/C3D_AlphaBlend
 * directly, though, so the cache can still say "already applied" while the
 * real GPU is left in the fog state. Azahar is forgiving here; real PICA200
 * hardware is not and can show temporal castle/window/ground flicker.
 *
 * Keep an independent copy of the state requested by gfx_pc and reassert it
 * after each backend draw. This is intentionally New-3DS-only and can be
 * removed once the renderer's internal state cache tracks temporary passes.
 */

static void (*sOriginalSetDepthTest)(bool enabled) = NULL;
static void (*sOriginalSetDepthMask)(bool enabled) = NULL;
static void (*sOriginalSetDepthDecal)(bool enabled) = NULL;
static void (*sOriginalSetUseAlpha)(bool enabled) = NULL;
static void (*sOriginalDrawTriangles)(float buf_vbo[], size_t buf_vbo_len, size_t triangle_count) = NULL;

static bool sDepthTest = false;
static bool sDepthWrite = true;
static bool sDepthDecal = false;
static bool sUseAlpha = false;
static bool sInstalled = false;

static void new3ds_gfx_guard_apply_state(void) {
    C3D_DepthTest(
        sDepthTest,
        GPU_LEQUAL,
        sDepthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    C3D_DepthMap(true, -1.0f, sDepthDecal ? -0.001f : 0.0f);

    if (sUseAlpha) {
        C3D_AlphaBlend(
            GPU_BLEND_ADD, GPU_BLEND_ADD,
            GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
            GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    } else {
        C3D_AlphaBlend(
            GPU_BLEND_ADD, GPU_BLEND_ADD,
            GPU_ONE, GPU_ZERO,
            GPU_ONE, GPU_ZERO);
    }
}

static void new3ds_gfx_guard_set_depth_test(bool enabled) {
    sDepthTest = enabled;
    sOriginalSetDepthTest(enabled);
}

static void new3ds_gfx_guard_set_depth_mask(bool enabled) {
    sDepthWrite = enabled;
    sOriginalSetDepthMask(enabled);
}

static void new3ds_gfx_guard_set_depth_decal(bool enabled) {
    sDepthDecal = enabled;
    sOriginalSetDepthDecal(enabled);
}

static void new3ds_gfx_guard_set_use_alpha(bool enabled) {
    sUseAlpha = enabled;
    sOriginalSetUseAlpha(enabled);
}

static void new3ds_gfx_guard_draw_triangles(
    float buf_vbo[],
    size_t buf_vbo_len,
    size_t triangle_count) {
    sOriginalDrawTriangles(buf_vbo, buf_vbo_len, triangle_count);

    /*
     * The fog helper is allowed to issue raw Citro3D state commands. Reassert
     * the gfx_pc-requested state after it returns so the next draw starts from
     * deterministic PICA200 state even when the renderer's cache skipped its
     * own restore call.
     */
    new3ds_gfx_guard_apply_state();
}

static void new3ds_gfx_state_guard_install(void) __attribute__((constructor));

static void new3ds_gfx_state_guard_install(void) {
    if (sInstalled) {
        return;
    }

    sOriginalSetDepthTest = gfx_citro3d_new3ds_api.set_depth_test;
    sOriginalSetDepthMask = gfx_citro3d_new3ds_api.set_depth_mask;
    sOriginalSetDepthDecal = gfx_citro3d_new3ds_api.set_zmode_decal;
    sOriginalSetUseAlpha = gfx_citro3d_new3ds_api.set_use_alpha;
    sOriginalDrawTriangles = gfx_citro3d_new3ds_api.draw_triangles;

    if (sOriginalSetDepthTest == NULL ||
        sOriginalSetDepthMask == NULL ||
        sOriginalSetDepthDecal == NULL ||
        sOriginalSetUseAlpha == NULL ||
        sOriginalDrawTriangles == NULL) {
        return;
    }

    gfx_citro3d_new3ds_api.set_depth_test = new3ds_gfx_guard_set_depth_test;
    gfx_citro3d_new3ds_api.set_depth_mask = new3ds_gfx_guard_set_depth_mask;
    gfx_citro3d_new3ds_api.set_zmode_decal = new3ds_gfx_guard_set_depth_decal;
    gfx_citro3d_new3ds_api.set_use_alpha = new3ds_gfx_guard_set_use_alpha;
    gfx_citro3d_new3ds_api.draw_triangles = new3ds_gfx_guard_draw_triangles;
    sInstalled = true;
}

#endif /* __3DS__ */
