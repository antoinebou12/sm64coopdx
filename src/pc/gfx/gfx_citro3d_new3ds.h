#pragma once

#ifdef __3DS__

#include <stddef.h>
#include <stdint.h>

#include "gfx_rendering_api.h"

typedef struct New3dsGfxStats {
    float frame_ms;
    float avg_frame_ms;
    uint32_t triangle_count;
    uint32_t vertex_count;
    uint32_t texture_count;
    uint32_t vbo_fill_percent;
    uint32_t degraded_draws;
    uint32_t dropped_draws;
} New3dsGfxStats;

extern struct GfxRenderingAPI gfx_citro3d_new3ds_api;

void new3ds_gfx_get_stats(New3dsGfxStats *out);

#endif

