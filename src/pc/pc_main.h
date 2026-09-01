#ifndef _PC_MAIN_H
#define _PC_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gfx/gfx_pc.h"

#if defined(__3DS__)
#include "gfx/gfx_citro3d_new3ds.h"
#include "gfx/gfx_dummy.h"

#include "audio/audio_api.h"
#include "audio/audio_new3ds.h"
#include "audio/audio_null.h"

/*
 * Keep pc_main.c's existing backend selection logic intact while the console
 * target is being integrated. On New 3DS, the "OpenGL" enum slot represents
 * the native PICA200 backend and SDL audio represents NDSP.
 */
#define gfx_opengl_api gfx_citro3d_new3ds_api
#define audio_sdl audio_new3ds
#else
#include "gfx/gfx_opengl.h"
#include "gfx/gfx_direct3d11.h"

#include "gfx/gfx_window_opengl.h"
#include "gfx/gfx_window_dxgi.h"
#include "gfx/gfx_dummy.h"

#include "audio/audio_api.h"
#include "audio/audio_sdl.h"
#include "audio/audio_null.h"
#endif

#ifdef GIT_HASH
#define TITLE ({ char title[96] = ""; snprintf(title, 96, "%s %s, [%s]", WINDOW_NAME, get_version(), GIT_HASH); title; })
#else
#define TITLE ({ char title[96] = ""; snprintf(title, 96, "%s %s", WINDOW_NAME, get_version()); title; })
#endif

#define AT_STARTUP __attribute__((constructor))

extern struct AudioAPI* gAudioApi;
extern struct GfxRenderingAPI* gRenderApi;

extern bool gGameInited;
extern bool gGfxInited;

extern f32 gMasterVolume;

extern u8 gLuaVolumeMaster;
extern u8 gLuaVolumeLevel;
extern u8 gLuaVolumeSfx;
extern u8 gLuaVolumeEnv;

void produce_one_dummy_frame(void (*callback)(), u8 clearColorR, u8 clearColorG, u8 clearColorB);
void game_deinit(void);
void game_exit(void);

#ifdef __cplusplus
}
#endif

#endif // _PC_MAIN_H
