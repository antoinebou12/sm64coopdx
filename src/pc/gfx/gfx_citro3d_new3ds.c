#ifdef __3DS__

#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
/*
 * Only the G_TX wrap/clamp/mirror bits from gbi.h are needed here. Avoid
 * <PR/gbi.h> because it pulls in ultratypes.h, whose s32/u32 typedefs clash
 * with libctru on devkitARM.
 */
#define NEW3DS_G_TX_WRAP 0
#define NEW3DS_G_TX_MIRROR 0x1
#define NEW3DS_G_TX_CLAMP 0x2

#include "gfx_citro3d_new3ds.h"
#include "gfx_cc.h"
#include "pc/configfile.h"
#include "pc/platform/new3ds/new3ds_log.h"
#include "pc/platform/new3ds/new3ds_platform_ui.h"
#include "pc/platform/new3ds/new3ds_bottom_ui.h"
#include "pc/gfx/gfx_window_manager.h"

/*
 * Citro3D backend for the New Nintendo 3DS port.
 *
 * This is intentionally based on the proven SM64 3DS rendering strategy, but
 * adapted to CoopDX's current ColorCombiner API and two-texture vertex layout.
 * PICA200 exposes one interpolated primary vertex color to the texture combiner,
 * so the backend treats the normal SHADE input as the varying color and folds
 * primitive/environment inputs into per-stage TEV constants.
 *
 * Extended CoopDX effects that require multiple unrelated varying colors,
 * fragment noise, or desktop post-processing are detected and reported once
 * per combiner instead of silently pretending to support them.
 */

static inline void new3ds_texenv_src(C3D_TexEnv *env, C3D_TexEnvMode mode, GPU_TEVSRC s1) {
    C3D_TexEnvSrc(env, mode, s1, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
}

static inline void new3ds_texenv_op_rgb(C3D_TexEnv *env, GPU_TEVOP_RGB o1) {
    C3D_TexEnvOpRgb(env, o1, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
}

static inline void new3ds_texenv_op_alpha(C3D_TexEnv *env, GPU_TEVOP_A o1) {
    C3D_TexEnvOpAlpha(env, o1, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
}

#define NEW3DS_SHADER_POOL_SIZE CC_MAX_SHADERS
#define NEW3DS_TEXTURE_POOL_SIZE 768
#define NEW3DS_GPU_VERTEX_FLOATS 12
#define NEW3DS_VBO_BYTES (2 * 1024 * 1024)
#define NEW3DS_MAX_INPUTS 8
#define NEW3DS_GFX_STATS_FRAMES 60
#define NEW3DS_PERF_LOG_INTERVAL_MS 5000

extern const u8 new3ds_shader_shbin[];
extern const u32 new3ds_shader_shbin_size;

struct ShaderProgram {
    uint64_t hash;
    uint8_t num_inputs;
    bool used_textures[2];
    bool use_alpha;
    bool use_fog;
    bool use_2cycle;
    bool texture_edge;
    bool light_map;
    bool world_geometry;
    bool uses_noise;
    bool warned_degraded;
    uint8_t shader_commands[SHADER_CMD_LENGTH];
    uint8_t input_mapping[NEW3DS_MAX_INPUTS];
    uint16_t num_floats;
    int16_t tex_offset[2];
    int16_t fog_offset;
    int16_t lightmap_offset;
    int16_t input_offset;
};

typedef struct New3dsTexture {
    C3D_Tex texture;
    bool initialized;
    bool linear_filter;
    uint32_t cms;
    uint32_t cmt;
    float scale_s;
    float scale_t;
} New3dsTexture;

typedef struct New3dsStageConstant {
    bool rgb_set;
    bool alpha_set;
    uint32_t rgb24;
    uint8_t alpha;
} New3dsStageConstant;

typedef struct New3dsSourceSpec {
    GPU_TEVSRC src;
    GPU_TEVOP_RGB rgb_op;
    GPU_TEVOP_A alpha_op;
} New3dsSourceSpec;

typedef struct New3dsDrawInfo {
    int primary_input;
    int primary_base;
    bool degraded;
    bool input_constant[NEW3DS_MAX_INPUTS];
    uint32_t input_color[NEW3DS_MAX_INPUTS];
} New3dsDrawInfo;

static DVLB_s *sVertexShaderDvlb = NULL;
static shaderProgram_s sVertexShaderProgram;
static C3D_RenderTarget *sTopTarget = NULL;
static float *sGpuVbo = NULL;
static size_t sGpuVertexIndex = 0;
static bool sFrameOpen = false;
static bool sInitialized = false;

static struct ShaderProgram sShaderPrograms[NEW3DS_SHADER_POOL_SIZE];
static uint8_t sShaderProgramCount = 0;
static uint8_t sShaderProgramIndex = 0;
static struct ShaderProgram *sCurrentProgram = NULL;

static New3dsTexture sTextures[NEW3DS_TEXTURE_POOL_SIZE];
static uint32_t sTextureCount = 0;
static uint32_t sBoundTexture[2] = { UINT32_MAX, UINT32_MAX };
static uint32_t sBoundTextureApplied[2] = { UINT32_MAX, UINT32_MAX };
static uint32_t sCurrentTexture = UINT32_MAX;

static bool sDepthTest = false;
static bool sDepthWrite = true;
static bool sDepthDecal = false;
static bool sUseAlpha = false;
static bool sDepthTestApplied = false;
static bool sDepthWriteApplied = true;
static bool sDepthDecalApplied = false;
static bool sUseAlphaApplied = false;
static uint32_t sDegradedDrawCount = 0;
static uint32_t sDroppedDrawCount = 0;

static uint32_t sFrameTriangles = 0;
static uint32_t sFrameVertices = 0;
static uint64_t sFrameStartMs = 0;
static float sFrameMsHistory[NEW3DS_GFX_STATS_FRAMES];
static uint32_t sFrameHistoryIndex = 0;
static uint32_t sFrameHistoryCount = 0;
static uint64_t sLastPerfLogMs = 0;
static New3dsGfxStats sGfxStats;

static const uint8_t sTileOrder[16] = {
    0, 1, 4, 5,
    2, 3, 6, 7,
    8, 9, 12, 13,
    10, 11, 14, 15,
};

static bool new3ds_z_is_from_0_to_1(void) {
    return true;
}

static uint8_t new3ds_float_to_u8(float value) {
    int out = (int)lrintf(value * 255.0f);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

static uint32_t new3ds_pack_color(float r, float g, float b, float a) {
    const uint32_t rr = new3ds_float_to_u8(r);
    const uint32_t gg = new3ds_float_to_u8(g);
    const uint32_t bb = new3ds_float_to_u8(b);
    const uint32_t aa = new3ds_float_to_u8(a);
    return rr | (gg << 8) | (bb << 16) | (aa << 24);
}

static uint32_t new3ds_next_pow2(uint32_t value) {
    if (value <= 8) return 8;
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

static void new3ds_apply_depth(void) {
    if (sDepthTest == sDepthTestApplied &&
        sDepthWrite == sDepthWriteApplied &&
        sDepthDecal == sDepthDecalApplied) {
        return;
    }
    C3D_DepthTest(sDepthTest, GPU_LEQUAL, sDepthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    C3D_DepthMap(true, -1.0f, sDepthDecal ? -0.001f : 0.0f);
    sDepthTestApplied = sDepthTest;
    sDepthWriteApplied = sDepthWrite;
    sDepthDecalApplied = sDepthDecal;
}

static void new3ds_apply_blend(void) {
    if (sUseAlpha == sUseAlphaApplied) {
        return;
    }
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
    sUseAlphaApplied = sUseAlpha;
}

static void new3ds_unload_shader(struct ShaderProgram *old_program) {
    (void)old_program;
}

static void new3ds_load_shader(struct ShaderProgram *program) {
    sCurrentProgram = program;
}

static int new3ds_input_base(uint8_t mapping) {
    switch (mapping) {
        case CC_SHADE:
        case CC_SHADEA:
            return 1;
        case CC_PRIM:
        case CC_PRIMA:
            return 2;
        case CC_ENV:
        case CC_ENVA:
            return 3;
        default:
            return 100 + mapping;
    }
}

static bool new3ds_mapping_is_alpha(uint8_t mapping) {
    return mapping == CC_PRIMA || mapping == CC_SHADEA || mapping == CC_ENVA;
}

static void new3ds_compute_layout(struct ShaderProgram *program) {
    int cursor = 4;
    for (int tile = 0; tile < 2; ++tile) {
        program->tex_offset[tile] = -1;
        if (program->used_textures[tile]) {
            program->tex_offset[tile] = (int16_t)cursor;
            cursor += 2;
        }
    }

    program->fog_offset = -1;
    if (program->use_fog) {
        program->fog_offset = (int16_t)cursor;
        cursor += 4;
    }

    program->lightmap_offset = -1;
    if (program->light_map) {
        program->lightmap_offset = (int16_t)cursor;
        cursor += 2;
    }

    program->input_offset = (int16_t)cursor;
    cursor += program->num_inputs * (program->use_alpha ? 4 : 3);
    program->num_floats = (uint16_t)cursor;
}

static struct ShaderProgram *new3ds_create_and_load_shader(struct ColorCombiner *cc) {
    struct CCFeatures features = {0};
    gfx_cc_get_features(cc, &features);

    struct ShaderProgram *program = &sShaderPrograms[sShaderProgramIndex];
    sShaderProgramIndex = (uint8_t)((sShaderProgramIndex + 1) % NEW3DS_SHADER_POOL_SIZE);
    if (sShaderProgramCount < NEW3DS_SHADER_POOL_SIZE) sShaderProgramCount++;

    memset(program, 0, sizeof(*program));
    program->hash = cc->hash;
    program->num_inputs = (uint8_t)features.num_inputs;
    if (program->num_inputs > NEW3DS_MAX_INPUTS) program->num_inputs = NEW3DS_MAX_INPUTS;
    program->used_textures[0] = features.used_textures[0];
    program->used_textures[1] = features.used_textures[1];
    program->use_alpha = cc->cm.use_alpha;
    program->use_fog = cc->cm.use_fog;
    program->use_2cycle = cc->cm.use_2cycle;
    program->texture_edge = cc->cm.texture_edge;
    program->light_map = cc->cm.light_map;
    program->world_geometry = cc->cm.world_geometry;
    program->uses_noise = features.do_noise;
    memcpy(program->shader_commands, cc->shader_commands, sizeof(program->shader_commands));
    memcpy(program->input_mapping, cc->shader_input_mapping, sizeof(program->input_mapping));
    new3ds_compute_layout(program);

    if (features.num_inputs > NEW3DS_MAX_INPUTS || features.do_noise || cc->cm.world_geometry || cc->cm.light_map) {
        program->warned_degraded = false;
    }

    new3ds_load_shader(program);
    return program;
}

static struct ShaderProgram *new3ds_lookup_shader(struct ColorCombiner *cc) {
    for (uint8_t i = 0; i < sShaderProgramCount; ++i) {
        if (sShaderPrograms[i].hash == cc->hash) return &sShaderPrograms[i];
    }
    return NULL;
}

static void new3ds_shader_get_info(struct ShaderProgram *program, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = program->num_inputs;
    used_textures[0] = program->used_textures[0];
    used_textures[1] = program->used_textures[1];
}

static uint32_t new3ds_new_texture(void) {
    if (sTextureCount >= NEW3DS_TEXTURE_POOL_SIZE) {
        sDroppedDrawCount++;
        NEW3DS_LOG_WARN_CAT(
            NEW3DS_LOG_CAT_GFX,
            "gfx",
            "texture pool exhausted (%u)",
            NEW3DS_TEXTURE_POOL_SIZE);
        return 0;
    }

    New3dsTexture *texture = &sTextures[sTextureCount];
    memset(texture, 0, sizeof(*texture));
    texture->scale_s = 1.0f;
    texture->scale_t = 1.0f;
    texture->cms = NEW3DS_G_TX_WRAP;
    texture->cmt = NEW3DS_G_TX_WRAP;
    return sTextureCount++;
}

static void new3ds_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1 || texture_id >= sTextureCount) return;
    sBoundTexture[tile] = texture_id;
    sCurrentTexture = texture_id;
    if (sBoundTextureApplied[tile] == texture_id) {
        return;
    }
    if (sTextures[texture_id].initialized) {
        C3D_TexBind(tile, &sTextures[texture_id].texture);
        sBoundTextureApplied[tile] = texture_id;
    }
}

static void new3ds_swizzle_rgba8(
    const uint8_t *src,
    uint32_t *dst,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t padded_width,
    uint32_t padded_height) {
    size_t dst_offset = 0;
    for (uint32_t y = 0; y < padded_height; y += 8) {
        for (uint32_t x = 0; x < padded_width; x += 8) {
            for (int i = 0; i < 64; ++i) {
                const int x2 = i & 7;
                const int y2 = i >> 3;
                const uint32_t real_x = (x + (uint32_t)x2) % src_width;
                const uint32_t real_y = (y + (uint32_t)y2) % src_height;
                const int pos = sTileOrder[(x2 & 3) + ((y2 & 3) << 2)]
                    + ((x2 >> 2) << 4) + ((y2 >> 2) << 5);
                const uint8_t *pixel = src + ((real_y * src_width + real_x) * 4);
                dst[dst_offset + (size_t)pos] =
                    ((uint32_t)pixel[0] << 24) |
                    ((uint32_t)pixel[1] << 16) |
                    ((uint32_t)pixel[2] << 8) |
                    (uint32_t)pixel[3];
            }
            dst_offset += 64;
        }
    }
}

static GPU_TEXTURE_WRAP_PARAM new3ds_wrap_mode(uint32_t value) {
    if (value & NEW3DS_G_TX_CLAMP) return GPU_CLAMP_TO_EDGE;
    return (value & NEW3DS_G_TX_MIRROR) ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
}

static void new3ds_apply_texture_parameters(uint32_t texture_id) {
    if (texture_id >= sTextureCount) return;
    New3dsTexture *texture = &sTextures[texture_id];
    if (!texture->initialized) return;

    C3D_TexSetFilter(
        &texture->texture,
        texture->linear_filter ? GPU_LINEAR : GPU_NEAREST,
        texture->linear_filter ? GPU_LINEAR : GPU_NEAREST);
    C3D_TexSetWrap(
        &texture->texture,
        new3ds_wrap_mode(texture->cms),
        new3ds_wrap_mode(texture->cmt));
}

/* PICA200 max is 1024x1024. Oversized sources (e.g. coopdx logo 2048x1024)
 * must be downsampled or upload is skipped and the previous bind (often sky)
 * stays visible in DJUI. */
static uint8_t *new3ds_downsample_rgba8(
    const uint8_t *src,
    int src_w,
    int src_h,
    int *out_w,
    int *out_h) {
    int dst_w = src_w;
    int dst_h = src_h;
    while (new3ds_next_pow2((uint32_t)dst_w) > 1024 || new3ds_next_pow2((uint32_t)dst_h) > 1024) {
        dst_w = (dst_w + 1) / 2;
        dst_h = (dst_h + 1) / 2;
        if (dst_w < 1 || dst_h < 1) {
            return NULL;
        }
    }

    uint8_t *dst = (uint8_t *)malloc((size_t)dst_w * (size_t)dst_h * 4);
    if (dst == NULL) {
        return NULL;
    }

    for (int y = 0; y < dst_h; ++y) {
        const int src_y = (y * src_h) / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            const int src_x = (x * src_w) / dst_w;
            const uint8_t *sp = src + (((size_t)src_y * (size_t)src_w + (size_t)src_x) * 4);
            uint8_t *dp = dst + (((size_t)y * (size_t)dst_w + (size_t)x) * 4);
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }

    *out_w = dst_w;
    *out_h = dst_h;
    return dst;
}

static void new3ds_upload_texture(const uint8_t *rgba32, int width, int height) {
    if (sCurrentTexture == UINT32_MAX || sCurrentTexture >= sTextureCount) return;
    if (rgba32 == NULL || width <= 0 || height <= 0) return;

    const uint8_t *upload_src = rgba32;
    int upload_w = width;
    int upload_h = height;
    uint8_t *downscaled = NULL;

    uint32_t padded_width = new3ds_next_pow2((uint32_t)upload_w);
    uint32_t padded_height = new3ds_next_pow2((uint32_t)upload_h);
    if (padded_width > 1024 || padded_height > 1024) {
        downscaled = new3ds_downsample_rgba8(rgba32, width, height, &upload_w, &upload_h);
        if (downscaled == NULL) {
            sDroppedDrawCount++;
            NEW3DS_LOG_WARN_CAT(
                NEW3DS_LOG_CAT_GFX,
                "gfx",
                "texture too large for PICA200 and downsample failed (%dx%d)",
                width,
                height);
            return;
        }
        upload_src = downscaled;
        padded_width = new3ds_next_pow2((uint32_t)upload_w);
        padded_height = new3ds_next_pow2((uint32_t)upload_h);
        NEW3DS_LOG_INFO_CAT(
            NEW3DS_LOG_CAT_GFX,
            "gfx",
            "downsampled oversized texture %dx%d -> %dx%d",
            width,
            height,
            upload_w,
            upload_h);
    }

    const size_t bytes = (size_t)padded_width * (size_t)padded_height * 4;
    uint32_t *swizzled = (uint32_t *)linearAlloc(bytes);
    if (swizzled == NULL) {
        free(downscaled);
        sDroppedDrawCount++;
        return;
    }

    new3ds_swizzle_rgba8(
        upload_src,
        swizzled,
        (uint32_t)upload_w,
        (uint32_t)upload_h,
        padded_width,
        padded_height);
    free(downscaled);
    downscaled = NULL;

    New3dsTexture *texture = &sTextures[sCurrentTexture];
    if (texture->initialized) {
        C3D_TexDelete(&texture->texture);
        texture->initialized = false;
    }

    if (!C3D_TexInit(&texture->texture, (u16)padded_width, (u16)padded_height, GPU_RGBA8)) {
        linearFree(swizzled);
        sDroppedDrawCount++;
        return;
    }

    C3D_TexUpload(&texture->texture, swizzled);
    C3D_TexFlush(&texture->texture);
    linearFree(swizzled);

    texture->initialized = true;
    texture->scale_s = (float)upload_w / (float)padded_width;
    texture->scale_t = (float)upload_h / (float)padded_height;
    new3ds_apply_texture_parameters(sCurrentTexture);

    for (int tile = 0; tile < 2; ++tile) {
        if (sBoundTexture[tile] == sCurrentTexture) {
            C3D_TexBind(tile, &texture->texture);
            sBoundTextureApplied[tile] = sCurrentTexture;
        }
    }
}

static void new3ds_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile > 1) return;
    const uint32_t texture_id = sBoundTexture[tile];
    if (texture_id == UINT32_MAX || texture_id >= sTextureCount) return;

    New3dsTexture *texture = &sTextures[texture_id];
    texture->linear_filter = linear_filter;
    texture->cms = cms;
    texture->cmt = cmt;
    new3ds_apply_texture_parameters(texture_id);
}

static void new3ds_set_depth_test(bool enabled) {
    sDepthTest = enabled;
    new3ds_apply_depth();
}

static void new3ds_set_depth_mask(bool enabled) {
    sDepthWrite = enabled;
    new3ds_apply_depth();
}

static void new3ds_set_zmode_decal(bool enabled) {
    sDepthDecal = enabled;
    new3ds_apply_depth();
}

static void new3ds_set_viewport(int x, int y, int width, int height) {
    /*
     * Landscape gfx_pc (400x240, Y down) → Citro3D target 240x400.
     * Vertices already rotate clip-space (x'=y, y'=-x). Swap axes here and
     * flip the 240-tall axis so HUD scissors align with the top of the screen.
     */
    if (width <= 0 || height <= 0) {
        return;
    }
    C3D_SetViewport(240 - (y + height), x, height, width);
}

static void new3ds_set_scissor(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        return;
    }
    const int gx1 = 240 - (y + height);
    const int gy1 = x;
    const int gx2 = 240 - y;
    const int gy2 = x + width;
    C3D_SetScissor(GPU_SCISSOR_NORMAL, gx1, gy1, gx2, gy2);
}

static void new3ds_set_use_alpha(bool enabled) {
    sUseAlpha = enabled;
    new3ds_apply_blend();
}

static uint32_t new3ds_read_input_color(
    const struct ShaderProgram *program,
    const float *buf,
    size_t vertex,
    int input) {
    if (input < 0 || input >= program->num_inputs) return 0xFFFFFFFF;
    const size_t stride = program->num_floats;
    const size_t components = program->use_alpha ? 4 : 3;
    const size_t offset = vertex * stride + (size_t)program->input_offset + (size_t)input * components;
    const float alpha = program->use_alpha ? buf[offset + 3] : 1.0f;
    return new3ds_pack_color(buf[offset], buf[offset + 1], buf[offset + 2], alpha);
}

static bool new3ds_input_is_constant(
    const struct ShaderProgram *program,
    const float *buf,
    size_t vertices,
    int input,
    uint32_t first_color) {
    for (size_t vertex = 1; vertex < vertices; ++vertex) {
        if (new3ds_read_input_color(program, buf, vertex, input) != first_color) return false;
    }
    return true;
}

static New3dsDrawInfo new3ds_analyze_draw(
    const struct ShaderProgram *program,
    const float *buf,
    size_t vertices) {
    New3dsDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.primary_input = -1;
    info.primary_base = -1;

    for (int input = 0; input < program->num_inputs; ++input) {
        info.input_color[input] = new3ds_read_input_color(program, buf, 0, input);
        info.input_constant[input] = new3ds_input_is_constant(
            program, buf, vertices, input, info.input_color[input]);
    }

    /*
     * Prefer SHADE as the interpolated primary even when the draw is flat.
     * Dialog boxes use G_CC_FADE (SHADE*ENV). If every input is treated as a
     * TEV constant, SHADE and ENV collide in one C3D_TexEnvColor and the box
     * becomes opaque white with invisible text.
     */
    for (int input = 0; input < program->num_inputs; ++input) {
        if (!info.input_constant[input] && program->input_mapping[input] == CC_SHADE) {
            info.primary_input = input;
            break;
        }
    }
    if (info.primary_input < 0) {
        for (int input = 0; input < program->num_inputs; ++input) {
            if (!info.input_constant[input] && program->input_mapping[input] == CC_SHADEA) {
                info.primary_input = input;
                break;
            }
        }
    }
    if (info.primary_input < 0) {
        for (int input = 0; input < program->num_inputs; ++input) {
            if (!info.input_constant[input]) {
                info.primary_input = input;
                break;
            }
        }
    }
    if (info.primary_input < 0) {
        for (int input = 0; input < program->num_inputs; ++input) {
            if (program->input_mapping[input] == CC_SHADE || program->input_mapping[input] == CC_SHADEA) {
                info.primary_input = input;
                break;
            }
        }
    }
    if (info.primary_input < 0 && program->num_inputs > 0) {
        info.primary_input = 0;
    }

    if (info.primary_input >= 0) {
        info.primary_base = new3ds_input_base(program->input_mapping[info.primary_input]);
        for (int input = 0; input < program->num_inputs; ++input) {
            if (!info.input_constant[input]
                && new3ds_input_base(program->input_mapping[input]) != info.primary_base) {
                info.degraded = true;
            }
        }
    }

    if (program->uses_noise || program->world_geometry || program->light_map) {
        info.degraded = true;
    }
    return info;
}

static bool new3ds_stage_set_rgb(New3dsStageConstant *constant, uint32_t packed) {
    const uint32_t rgb = packed & 0x00FFFFFFu;
    if (constant->rgb_set && constant->rgb24 != rgb) return false;
    constant->rgb_set = true;
    constant->rgb24 = rgb;
    return true;
}

static bool new3ds_stage_set_alpha(New3dsStageConstant *constant, uint32_t packed) {
    const uint8_t alpha = (uint8_t)(packed >> 24);
    if (constant->alpha_set && constant->alpha != alpha) return false;
    constant->alpha_set = true;
    constant->alpha = alpha;
    return true;
}

static void new3ds_apply_stage_constant(C3D_TexEnv *env, const New3dsStageConstant *constant) {
    /* Unset channels default to transparent black — never opaque white. */
    uint32_t color = constant->rgb_set ? constant->rgb24 : 0x00000000u;
    color |= (uint32_t)(constant->alpha_set ? constant->alpha : 0x00u) << 24;
    C3D_TexEnvColor(env, color);
}

static New3dsSourceSpec new3ds_resolve_source(
    const struct ShaderProgram *program,
    New3dsDrawInfo *draw,
    uint8_t item,
    int cycle,
    bool alpha_channel,
    New3dsStageConstant *constant) {
    New3dsSourceSpec spec = {
        .src = GPU_PREVIOUS_BUFFER,
        .rgb_op = GPU_TEVOP_RGB_SRC_COLOR,
        .alpha_op = GPU_TEVOP_A_SRC_ALPHA,
    };

    if (item >= SHADER_INPUT_1 && item <= SHADER_INPUT_8) {
        const int input = item - SHADER_INPUT_1;
        if (input >= program->num_inputs) {
            draw->degraded = true;
            return spec;
        }

        const uint8_t mapping = program->input_mapping[input];
        const int base = new3ds_input_base(mapping);
        if (draw->primary_input >= 0 && base == draw->primary_base) {
            spec.src = GPU_PRIMARY_COLOR;
            if (new3ds_mapping_is_alpha(mapping)) {
                spec.rgb_op = GPU_TEVOP_RGB_SRC_ALPHA;
            }
            return spec;
        }

        spec.src = GPU_CONSTANT;
        const bool ok = alpha_channel
            ? new3ds_stage_set_alpha(constant, draw->input_color[input])
            : new3ds_stage_set_rgb(constant, draw->input_color[input]);
        if (!ok) draw->degraded = true;
        return spec;
    }

    switch (item) {
        case SHADER_0:
            spec.src = GPU_PREVIOUS_BUFFER;
            break;
        case SHADER_1:
            spec.src = GPU_PREVIOUS_BUFFER;
            spec.rgb_op = GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
            spec.alpha_op = GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA;
            break;
        case SHADER_TEXEL0:
            spec.src = GPU_TEXTURE0;
            break;
        case SHADER_TEXEL0A:
            spec.src = GPU_TEXTURE0;
            spec.rgb_op = GPU_TEVOP_RGB_SRC_ALPHA;
            break;
        case SHADER_TEXEL1:
            spec.src = GPU_TEXTURE1;
            break;
        case SHADER_TEXEL1A:
            spec.src = GPU_TEXTURE1;
            spec.rgb_op = GPU_TEVOP_RGB_SRC_ALPHA;
            break;
        case SHADER_COMBINED:
            spec.src = cycle == 0 ? GPU_PREVIOUS_BUFFER : GPU_PREVIOUS;
            break;
        case SHADER_COMBINEDA:
            spec.src = cycle == 0 ? GPU_PREVIOUS_BUFFER : GPU_PREVIOUS;
            spec.rgb_op = GPU_TEVOP_RGB_SRC_ALPHA;
            break;
        case SHADER_NOISE:
            draw->degraded = true;
            spec.src = GPU_PREVIOUS_BUFFER;
            break;
        default:
            draw->degraded = true;
            break;
    }

    return spec;
}

static void new3ds_set_rgb_formula(
    C3D_TexEnv *stage_a,
    C3D_TexEnv *stage_b,
    New3dsStageConstant *constant_a,
    New3dsStageConstant *constant_b,
    const struct ShaderProgram *program,
    New3dsDrawInfo *draw,
    const uint8_t *cmd,
    int cycle) {
    const bool single = cmd[2] == SHADER_0;
    const bool multiply = cmd[1] == SHADER_0 && cmd[3] == SHADER_0;
    const bool mix = cmd[1] == cmd[3];

    if (single || multiply || mix) {
        C3D_TexEnvFunc(stage_a, C3D_RGB, GPU_REPLACE);
        new3ds_texenv_src(stage_a, C3D_RGB, cycle == 0 ? GPU_PREVIOUS_BUFFER : GPU_PREVIOUS);
        new3ds_texenv_op_rgb(stage_a, GPU_TEVOP_RGB_SRC_COLOR);

        if (single) {
            New3dsSourceSpec d = new3ds_resolve_source(program, draw, cmd[3], cycle, false, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_RGB, GPU_REPLACE);
            new3ds_texenv_src(stage_b, C3D_RGB, d.src);
            new3ds_texenv_op_rgb(stage_b, d.rgb_op);
        } else if (multiply) {
            New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, false, constant_b);
            New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, false, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_RGB, GPU_MODULATE);
            C3D_TexEnvSrc(stage_b, C3D_RGB, a.src, c.src, GPU_PREVIOUS_BUFFER);
            C3D_TexEnvOpRgb(stage_b, a.rgb_op, c.rgb_op, GPU_TEVOP_RGB_SRC_COLOR);
        } else {
            New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, false, constant_b);
            New3dsSourceSpec b = new3ds_resolve_source(program, draw, cmd[1], cycle, false, constant_b);
            New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, false, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_RGB, GPU_INTERPOLATE);
            C3D_TexEnvSrc(stage_b, C3D_RGB, a.src, b.src, c.src);
            C3D_TexEnvOpRgb(stage_b, a.rgb_op, b.rgb_op, c.rgb_op);
        }
        return;
    }

    New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, false, constant_a);
    New3dsSourceSpec b = new3ds_resolve_source(program, draw, cmd[1], cycle, false, constant_a);
    C3D_TexEnvFunc(stage_a, C3D_RGB, GPU_SUBTRACT);
    C3D_TexEnvSrc(stage_a, C3D_RGB, a.src, b.src, GPU_PREVIOUS_BUFFER);
    C3D_TexEnvOpRgb(stage_a, a.rgb_op, b.rgb_op, GPU_TEVOP_RGB_SRC_COLOR);

    if (cycle > 0 && (cmd[2] == SHADER_COMBINED || cmd[2] == SHADER_COMBINEDA
        || cmd[3] == SHADER_COMBINED || cmd[3] == SHADER_COMBINEDA)) {
        draw->degraded = true;
    }

    New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, false, constant_b);
    New3dsSourceSpec d = new3ds_resolve_source(program, draw, cmd[3], cycle, false, constant_b);
    C3D_TexEnvFunc(stage_b, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvSrc(stage_b, C3D_RGB, GPU_PREVIOUS, c.src, d.src);
    C3D_TexEnvOpRgb(stage_b, GPU_TEVOP_RGB_SRC_COLOR, c.rgb_op, d.rgb_op);
}

static void new3ds_set_alpha_formula(
    C3D_TexEnv *stage_a,
    C3D_TexEnv *stage_b,
    New3dsStageConstant *constant_a,
    New3dsStageConstant *constant_b,
    const struct ShaderProgram *program,
    New3dsDrawInfo *draw,
    const uint8_t *cmd,
    int cycle) {
    if (!program->use_alpha) {
        C3D_TexEnvFunc(stage_a, C3D_Alpha, GPU_REPLACE);
        new3ds_texenv_src(stage_a, C3D_Alpha, cycle == 0 ? GPU_PREVIOUS_BUFFER : GPU_PREVIOUS);
        new3ds_texenv_op_alpha(stage_a, GPU_TEVOP_A_SRC_ALPHA);
        C3D_TexEnvFunc(stage_b, C3D_Alpha, GPU_REPLACE);
        new3ds_texenv_src(stage_b, C3D_Alpha, GPU_PREVIOUS_BUFFER);
        new3ds_texenv_op_alpha(stage_b, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA);
        return;
    }

    const bool single = cmd[2] == SHADER_0;
    const bool multiply = cmd[1] == SHADER_0 && cmd[3] == SHADER_0;
    const bool mix = cmd[1] == cmd[3];

    if (single || multiply || mix) {
        /*
         * Do not seed alpha from PREVIOUS_BUFFER on cycle 0 — that leaves
         * translucent DJUI/N64 boxes opaque white. Prefer primary/texture alpha.
         */
        C3D_TexEnvFunc(stage_a, C3D_Alpha, GPU_REPLACE);
        if (cycle == 0) {
            new3ds_texenv_src(stage_a, C3D_Alpha, GPU_PRIMARY_COLOR);
        } else {
            new3ds_texenv_src(stage_a, C3D_Alpha, GPU_PREVIOUS);
        }
        new3ds_texenv_op_alpha(stage_a, GPU_TEVOP_A_SRC_ALPHA);

        if (single) {
            New3dsSourceSpec d = new3ds_resolve_source(program, draw, cmd[3], cycle, true, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_Alpha, GPU_REPLACE);
            new3ds_texenv_src(stage_b, C3D_Alpha, d.src);
            new3ds_texenv_op_alpha(stage_b, d.alpha_op);
        } else if (multiply) {
            New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, true, constant_b);
            New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, true, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_Alpha, GPU_MODULATE);
            C3D_TexEnvSrc(stage_b, C3D_Alpha, a.src, c.src, GPU_PRIMARY_COLOR);
            C3D_TexEnvOpAlpha(stage_b, a.alpha_op, c.alpha_op, GPU_TEVOP_A_SRC_ALPHA);
        } else {
            New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, true, constant_b);
            New3dsSourceSpec b = new3ds_resolve_source(program, draw, cmd[1], cycle, true, constant_b);
            New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, true, constant_b);
            C3D_TexEnvFunc(stage_b, C3D_Alpha, GPU_INTERPOLATE);
            C3D_TexEnvSrc(stage_b, C3D_Alpha, a.src, b.src, c.src);
            C3D_TexEnvOpAlpha(stage_b, a.alpha_op, b.alpha_op, c.alpha_op);
        }
        return;
    }

    New3dsSourceSpec a = new3ds_resolve_source(program, draw, cmd[0], cycle, true, constant_a);
    New3dsSourceSpec b = new3ds_resolve_source(program, draw, cmd[1], cycle, true, constant_a);
    C3D_TexEnvFunc(stage_a, C3D_Alpha, GPU_SUBTRACT);
    C3D_TexEnvSrc(stage_a, C3D_Alpha, a.src, b.src, GPU_PREVIOUS_BUFFER);
    C3D_TexEnvOpAlpha(stage_a, a.alpha_op, b.alpha_op, GPU_TEVOP_A_SRC_ALPHA);

    if (cycle > 0 && (cmd[2] == SHADER_COMBINED || cmd[2] == SHADER_COMBINEDA
        || cmd[3] == SHADER_COMBINED || cmd[3] == SHADER_COMBINEDA)) {
        draw->degraded = true;
    }

    New3dsSourceSpec c = new3ds_resolve_source(program, draw, cmd[2], cycle, true, constant_b);
    New3dsSourceSpec d = new3ds_resolve_source(program, draw, cmd[3], cycle, true, constant_b);
    C3D_TexEnvFunc(stage_b, C3D_Alpha, GPU_MULTIPLY_ADD);
    C3D_TexEnvSrc(stage_b, C3D_Alpha, GPU_PREVIOUS, c.src, d.src);
    C3D_TexEnvOpAlpha(stage_b, GPU_TEVOP_A_SRC_ALPHA, c.alpha_op, d.alpha_op);
}

static void new3ds_configure_program(struct ShaderProgram *program, New3dsDrawInfo *draw) {
    for (int stage = 0; stage < 6; ++stage) {
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    }
    C3D_TexEnvBufColor(0x00000000);
    C3D_TexEnvBufUpdate(0, 0);

    const int cycles = program->use_2cycle ? 2 : 1;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        C3D_TexEnv *stage_a = C3D_GetTexEnv(cycle * 2);
        C3D_TexEnv *stage_b = C3D_GetTexEnv(cycle * 2 + 1);
        New3dsStageConstant constant_a = {0};
        New3dsStageConstant constant_b = {0};
        const uint8_t *cmd = &program->shader_commands[cycle * 8];

        new3ds_set_rgb_formula(stage_a, stage_b, &constant_a, &constant_b, program, draw, cmd, cycle);
        new3ds_set_alpha_formula(stage_a, stage_b, &constant_a, &constant_b, program, draw, cmd + 4, cycle);
        new3ds_apply_stage_constant(stage_a, &constant_a);
        new3ds_apply_stage_constant(stage_b, &constant_b);
    }

    for (int stage = cycles * 2; stage < 6; ++stage) {
        C3D_TexEnv *env = C3D_GetTexEnv(stage);
        C3D_TexEnvInit(env);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        new3ds_texenv_src(env, C3D_Both, GPU_PREVIOUS);
    }

    /* Texture-edge (cutout) needs a higher threshold; translucent UI uses >0. */
    C3D_AlphaTest(true, GPU_GREATER, program->texture_edge && program->use_alpha ? 77 : 0);
}

static float new3ds_texture_scale_s(int tile) {
    const uint32_t id = sBoundTexture[tile];
    if (id == UINT32_MAX || id >= sTextureCount || !sTextures[id].initialized) return 1.0f;
    return sTextures[id].scale_s;
}

static float new3ds_texture_scale_t(int tile) {
    const uint32_t id = sBoundTexture[tile];
    if (id == UINT32_MAX || id >= sTextureCount || !sTextures[id].initialized) return 1.0f;
    return sTextures[id].scale_t;
}

static void new3ds_write_gpu_vertex(
    const struct ShaderProgram *program,
    const New3dsDrawInfo *draw,
    const float *src,
    float *dst) {
    dst[0] = src[1];
    dst[1] = -src[0];
    dst[2] = -src[2];
    dst[3] = src[3];

    /*
     * T maps straight through: the uploader writes the source top row first and
     * PICA200 T=0 addresses that row. The old `1.0f - v * scale` invert selected
     * the wrong DJUI atlas rows (garbled menu text) and was not pad-aware.
     *
     * HUD/DJUI draw with depth testing off. On the landscape→portrait clip
     * transform those quads need S mirrored so icons/glyphs are not left-right
     * flipped; 3D (depth on) keeps S unchanged.
     */
    dst[4] = 0.0f;
    dst[5] = 0.0f;
    if (program->tex_offset[0] >= 0) {
        const float s0 = src[program->tex_offset[0]];
        const float t0 = src[program->tex_offset[0] + 1];
        dst[4] = (sDepthTest ? s0 : (1.0f - s0)) * new3ds_texture_scale_s(0);
        dst[5] = t0 * new3ds_texture_scale_t(0);
    }

    dst[6] = 0.0f;
    dst[7] = 0.0f;
    int tex1_offset = program->tex_offset[1];
    if (program->light_map && program->lightmap_offset >= 0) tex1_offset = program->lightmap_offset;
    if (tex1_offset >= 0) {
        const float s1 = src[tex1_offset];
        const float t1 = src[tex1_offset + 1];
        dst[6] = (sDepthTest ? s1 : (1.0f - s1)) * new3ds_texture_scale_s(1);
        dst[7] = t1 * new3ds_texture_scale_t(1);
    }

    if (draw->primary_input >= 0) {
        const size_t components = program->use_alpha ? 4 : 3;
        const int input_offset = program->input_offset + draw->primary_input * (int)components;
        dst[8] = src[input_offset];
        dst[9] = src[input_offset + 1];
        dst[10] = src[input_offset + 2];
        dst[11] = program->use_alpha ? src[input_offset + 3] : 1.0f;
    } else {
        dst[8] = 1.0f;
        dst[9] = 1.0f;
        dst[10] = 1.0f;
        dst[11] = 1.0f;
    }
}

static bool new3ds_reserve_vertices(size_t vertices) {
    const size_t capacity = NEW3DS_VBO_BYTES / (NEW3DS_GPU_VERTEX_FLOATS * sizeof(float));
    if (sGpuVertexIndex + vertices > capacity) {
        sDroppedDrawCount++;
        return false;
    }
    return true;
}

static void new3ds_render_fog(
    const struct ShaderProgram *program,
    const float *buf,
    size_t vertices) {
    if (!program->use_fog || program->fog_offset < 0 || !new3ds_reserve_vertices(vertices)) return;

    for (int stage = 0; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    new3ds_texenv_src(env, C3D_Both, GPU_PRIMARY_COLOR);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_AlphaBlend(
        GPU_BLEND_ADD, GPU_BLEND_ADD,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_ZERO, GPU_DST_ALPHA);
    C3D_DepthTest(sDepthTest, GPU_LEQUAL, GPU_WRITE_COLOR);

    /*
     * Fog wrote depth/blend behind the state cache. Invalidate Applied so the
     * restores below cannot early-out and leave PICA200 stuck in fog state
     * (castle/window/ground flicker on real hardware).
     */
    sUseAlphaApplied = !sUseAlpha;
    sDepthTestApplied = !sDepthTest;
    sDepthWriteApplied = !sDepthWrite;
    sDepthDecalApplied = !sDepthDecal;

    const size_t start = sGpuVertexIndex;
    for (size_t vertex = 0; vertex < vertices; ++vertex) {
        const float *src = buf + vertex * program->num_floats;
        float *dst = sGpuVbo + sGpuVertexIndex * NEW3DS_GPU_VERTEX_FLOATS;
        dst[0] = src[1];
        dst[1] = -src[0];
        dst[2] = -src[2];
        dst[3] = src[3];
        dst[4] = dst[5] = dst[6] = dst[7] = 0.0f;
        dst[8] = src[program->fog_offset];
        dst[9] = src[program->fog_offset + 1];
        dst[10] = src[program->fog_offset + 2];
        dst[11] = src[program->fog_offset + 3];
        sGpuVertexIndex++;
    }

    C3D_DrawArrays(GPU_TRIANGLES, (int)start, (int)vertices);
    new3ds_apply_blend();
    new3ds_apply_depth();
}

static void new3ds_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t triangle_count) {
    (void)buf_vbo_len;
    if (sCurrentProgram == NULL || triangle_count == 0) return;

    const size_t vertices = triangle_count * 3;
    if (!new3ds_reserve_vertices(vertices)) return;

    New3dsDrawInfo draw = new3ds_analyze_draw(sCurrentProgram, buf_vbo, vertices);
    new3ds_configure_program(sCurrentProgram, &draw);

    if (draw.degraded) {
        sDegradedDrawCount++;
        if (!sCurrentProgram->warned_degraded) {
            NEW3DS_LOG_WARN_CAT(
                NEW3DS_LOG_CAT_GFX,
                "gfx",
                "degraded combiner %016llx inputs=%u 2cycle=%u noise=%u lightmap=%u post=%u",
                (unsigned long long)sCurrentProgram->hash,
                (unsigned int)sCurrentProgram->num_inputs,
                sCurrentProgram->use_2cycle ? 1u : 0u,
                sCurrentProgram->uses_noise ? 1u : 0u,
                sCurrentProgram->light_map ? 1u : 0u,
                sCurrentProgram->world_geometry ? 1u : 0u);
            sCurrentProgram->warned_degraded = true;
        }
    }

    const size_t start = sGpuVertexIndex;
    for (size_t vertex = 0; vertex < vertices; ++vertex) {
        const float *src = buf_vbo + vertex * sCurrentProgram->num_floats;
        float *dst = sGpuVbo + sGpuVertexIndex * NEW3DS_GPU_VERTEX_FLOATS;
        new3ds_write_gpu_vertex(sCurrentProgram, &draw, src, dst);
        sGpuVertexIndex++;
    }

    C3D_DrawArrays(GPU_TRIANGLES, (int)start, (int)vertices);
    sFrameTriangles += (uint32_t)triangle_count;
    sFrameVertices += (uint32_t)vertices;
    new3ds_render_fog(sCurrentProgram, buf_vbo, vertices);
}

static void new3ds_update_gfx_stats(float frame_ms) {
    const size_t capacity = NEW3DS_VBO_BYTES / (NEW3DS_GPU_VERTEX_FLOATS * sizeof(float));
    const uint32_t vbo_fill = capacity > 0
        ? (uint32_t)((sGpuVertexIndex * 100ULL) / capacity)
        : 0;

    sFrameMsHistory[sFrameHistoryIndex] = frame_ms;
    sFrameHistoryIndex = (sFrameHistoryIndex + 1) % NEW3DS_GFX_STATS_FRAMES;
    if (sFrameHistoryCount < NEW3DS_GFX_STATS_FRAMES) {
        sFrameHistoryCount++;
    }

    float avg_ms = 0.0f;
    for (uint32_t i = 0; i < sFrameHistoryCount; ++i) {
        avg_ms += sFrameMsHistory[i];
    }
    if (sFrameHistoryCount > 0) {
        avg_ms /= (float)sFrameHistoryCount;
    }

    sGfxStats.frame_ms = frame_ms;
    sGfxStats.avg_frame_ms = avg_ms;
    sGfxStats.triangle_count = sFrameTriangles;
    sGfxStats.vertex_count = sFrameVertices;
    sGfxStats.texture_count = sTextureCount;
    sGfxStats.vbo_fill_percent = vbo_fill;
    sGfxStats.degraded_draws = sDegradedDrawCount;
    sGfxStats.dropped_draws = sDroppedDrawCount;

    const uint64_t now_ms = osGetTime();
    if ((now_ms - sLastPerfLogMs) >= NEW3DS_PERF_LOG_INTERVAL_MS &&
        (configNew3dsLogPerf || configShowFPS)) {
        NEW3DS_LOG_INFO_CAT(
            NEW3DS_LOG_CAT_PERF,
            "gfx",
            "frame_ms=%.2f avg_ms=%.2f tris=%u verts=%u tex=%u vbo=%u%% degraded=%u dropped=%u",
            sGfxStats.frame_ms,
            sGfxStats.avg_frame_ms,
            sGfxStats.triangle_count,
            sGfxStats.vertex_count,
            sGfxStats.texture_count,
            sGfxStats.vbo_fill_percent,
            sGfxStats.degraded_draws,
            sGfxStats.dropped_draws);
        sLastPerfLogMs = now_ms;
    }
}

void new3ds_gfx_get_stats(New3dsGfxStats *out) {
    if (out != NULL) {
        *out = sGfxStats;
    }
}

static void new3ds_gfx_show_fatal_and_exit(const char *message) {
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_GFX, "gfx", "%s", message);
    new3ds_platform_show_exit_message(message);
    gfx_wm_shutdown();
    new3ds_platform_quit();
}

static void new3ds_init(void) {
    if (sInitialized) return;

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        new3ds_gfx_show_fatal_and_exit("Graphics initialization failed.\nCitro3D could not start.");
    }

    const u32 transfer_flags =
        GX_TRANSFER_FLIP_VERT(0) |
        GX_TRANSFER_OUT_TILED(0) |
        GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    sTopTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (sTopTarget == NULL) {
        new3ds_gfx_show_fatal_and_exit("Graphics initialization failed.\nCould not create the display.");
    }
    C3D_RenderTargetSetOutput(sTopTarget, GFX_TOP, GFX_LEFT, transfer_flags);

    sVertexShaderDvlb = DVLB_ParseFile((u32 *)new3ds_shader_shbin, new3ds_shader_shbin_size);
    if (sVertexShaderDvlb == NULL) {
        new3ds_gfx_show_fatal_and_exit("Graphics initialization failed.\nCould not load the GPU shader.");
    }

    shaderProgramInit(&sVertexShaderProgram);
    shaderProgramSetVsh(&sVertexShaderProgram, &sVertexShaderDvlb->DVLE[0]);
    C3D_BindProgram(&sVertexShaderProgram);

    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr_info, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr_info, 3, GPU_FLOAT, 4);

    sGpuVbo = (float *)linearAlloc(NEW3DS_VBO_BYTES);
    if (sGpuVbo == NULL) {
        new3ds_gfx_show_fatal_and_exit("Graphics initialization failed.\nOut of video memory.");
    }

    C3D_BufInfo *buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    BufInfo_Add(
        buf_info,
        sGpuVbo,
        NEW3DS_GPU_VERTEX_FLOATS * sizeof(float),
        4,
        0x3210);

    C3D_CullFace(GPU_CULL_NONE);
    C3D_TexEnvBufColor(0x00000000);
    C3D_TexEnvBufUpdate(0, 0);
    new3ds_apply_depth();
    new3ds_apply_blend();
    C3D_AlphaTest(true, GPU_GREATER, 0);

    sInitialized = true;
    NEW3DS_LOG_INFO_CAT(
        NEW3DS_LOG_CAT_GFX,
        "gfx",
        "initialized logical=400x240 gpu=240x400 fb=BGR8 tex_pool=%u vbo_bytes=%u",
        NEW3DS_TEXTURE_POOL_SIZE,
        (unsigned)NEW3DS_VBO_BYTES);
}

static void new3ds_on_resize(void) {
}

static void new3ds_bind_3d_pipeline(void) {
    C3D_BindProgram(&sVertexShaderProgram);

    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr_info, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr_info, 3, GPU_FLOAT, 4);

    C3D_BufInfo *buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    BufInfo_Add(
        buf_info,
        sGpuVbo,
        NEW3DS_GPU_VERTEX_FLOATS * sizeof(float),
        4,
        0x3210);

    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    sBoundTextureApplied[0] = UINT32_MAX;
    sBoundTextureApplied[1] = UINT32_MAX;
    sDepthTestApplied = !sDepthTest;
    sDepthWriteApplied = !sDepthWrite;
    sDepthDecalApplied = !sDepthDecal;
    sUseAlphaApplied = !sUseAlpha;
    new3ds_apply_depth();
    new3ds_apply_blend();
}

static void new3ds_start_frame(void) {
    if (!sInitialized || sFrameOpen) return;
    sGpuVertexIndex = 0;
    sFrameTriangles = 0;
    sFrameVertices = 0;
    sFrameStartMs = osGetTime();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(sTopTarget, C3D_CLEAR_ALL, 0x000000FF, 0xFFFFFFFF);
    C3D_FrameDrawOn(sTopTarget);
    new3ds_bind_3d_pipeline();
    sFrameOpen = true;
}

static void new3ds_end_frame(void) {
}

static void new3ds_finish_render(void) {
    if (!sFrameOpen) return;
    const float frame_ms = (float)(osGetTime() - sFrameStartMs);
    new3ds_update_gfx_stats(frame_ms);
    C3D_FrameEnd(0);
    sFrameOpen = false;
    /* CPU-blit logs into the bottom backbuffer after Citro3D swaps. */
    if (new3ds_bottom_ui_ready()) {
        new3ds_bottom_ui_draw();
    }
}

static const char *new3ds_get_name(void) {
    return "Citro3D / PICA200 (New 3DS)";
}

static void new3ds_shutdown(void) {
    if (!sInitialized) return;

    new3ds_bottom_ui_shutdown();

    if (sFrameOpen) {
        C3D_FrameEnd(0);
        sFrameOpen = false;
    }

    for (uint32_t i = 0; i < sTextureCount; ++i) {
        if (sTextures[i].initialized) {
            C3D_TexDelete(&sTextures[i].texture);
            sTextures[i].initialized = false;
        }
    }
    sTextureCount = 0;
    sBoundTexture[0] = UINT32_MAX;
    sBoundTexture[1] = UINT32_MAX;
    sBoundTextureApplied[0] = UINT32_MAX;
    sBoundTextureApplied[1] = UINT32_MAX;
    sCurrentTexture = UINT32_MAX;

    if (sGpuVbo != NULL) {
        linearFree(sGpuVbo);
        sGpuVbo = NULL;
    }
    if (sTopTarget != NULL) {
        C3D_RenderTargetDelete(sTopTarget);
        sTopTarget = NULL;
    }

    shaderProgramFree(&sVertexShaderProgram);
    if (sVertexShaderDvlb != NULL) {
        DVLB_Free(sVertexShaderDvlb);
        sVertexShaderDvlb = NULL;
    }

    NEW3DS_LOG_INFO_CAT(
        NEW3DS_LOG_CAT_GFX,
        "gfx",
        "shutdown degraded_draws=%lu dropped_draws=%lu",
        (unsigned long)sDegradedDrawCount,
        (unsigned long)sDroppedDrawCount);

    C3D_Fini();
    sCurrentProgram = NULL;
    sShaderProgramCount = 0;
    sShaderProgramIndex = 0;
    sInitialized = false;
}

struct GfxRenderingAPI gfx_citro3d_new3ds_api = {
    new3ds_z_is_from_0_to_1,
    new3ds_unload_shader,
    new3ds_load_shader,
    new3ds_create_and_load_shader,
    new3ds_lookup_shader,
    new3ds_shader_get_info,
    new3ds_new_texture,
    new3ds_select_texture,
    new3ds_upload_texture,
    new3ds_set_sampler_parameters,
    new3ds_set_depth_test,
    new3ds_set_depth_mask,
    new3ds_set_zmode_decal,
    new3ds_set_viewport,
    new3ds_set_scissor,
    new3ds_set_use_alpha,
    new3ds_draw_triangles,
    new3ds_init,
    new3ds_on_resize,
    new3ds_start_frame,
    new3ds_end_frame,
    new3ds_finish_render,
    new3ds_get_name,
    new3ds_shutdown,
};

#endif /* __3DS__ */
