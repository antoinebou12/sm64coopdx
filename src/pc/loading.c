#include "loading.h"

#include <assert.h>

#include "djui/djui.h"
#include "pc/djui/djui_unicode.h"

#include "pc_main.h"
#include "pc/utils/misc.h"
#include "pc/cliopts.h"
#include "rom_checker.h"
#include "pc/gfx/gfx_window_manager.h"
#if defined(__3DS__)
#include "pc/platform/new3ds/new3ds_boot_trace.h"
#include "pc/platform/new3ds/new3ds_boot_progress.h"
#include "pc/platform/new3ds/new3ds_platform_ui.h"
#endif

extern ALIGNED8 u8 texture_coopdx_logo[];

struct LoadingSegment gCurrLoadingSegment = { "", 0 };

struct LoadingScreen {
    struct DjuiBase base;
    struct DjuiImage* splashImage;
    struct DjuiText* splashText;
    struct DjuiText* loadingDesc;
    struct DjuiProgressBar *loadingBar;
};

static struct LoadingScreen* sLoading = NULL;

struct ThreadHandle gLoadingThread = { 0 };

void loading_screen_set_segment_text(const char* text) {
    snprintf(gCurrLoadingSegment.str, 256, "%s", text);
}

void loading_screen_reset_progress_bar(void) {
    sLoading->loadingBar->smoothValue = 0;
}

static void loading_screen_produce_frame_callback(void) {
    if (sLoading) { djui_base_render(&sLoading->base); }
}

static void loading_screen_produce_one_frame(void) {
    produce_one_dummy_frame(loading_screen_produce_frame_callback, 0x00, 0x00, 0x00);
}

static bool loading_screen_on_render(struct DjuiBase* base) {
    MUTEX_LOCK(gLoadingThread);

    u32 windowWidth, windowHeight;
    gfx_get_dimensions(&windowWidth, &windowHeight);
    f32 scale = djui_gfx_get_scale();
    windowWidth /= scale;
    windowHeight /= scale;

    f32 loadingDescY1 = windowHeight * 0.5f - sLoading->loadingDesc->base.height.value * 0.5f;
    f32 loadingDescY2 = windowHeight * 0.5f + sLoading->loadingDesc->base.height.value * 0.5f;

    // fill the screen
    djui_base_set_size(base, windowWidth, windowHeight);

    // splash logo
    if (configExCoopTheme) {
        djui_base_set_location(&sLoading->splashText->base, 0, loadingDescY1 - sLoading->splashText->base.height.value);
    } else {
        djui_base_set_location(&sLoading->splashImage->base, 0, loadingDescY1 - sLoading->splashImage->base.height.value);
    }

    {
        // loading text description
        char buffer[256] = "";
        u32 length = strlen(gCurrLoadingSegment.str);
        if (length > 0) {
            if (gCurrLoadingSegment.percentage > 0) {
                snprintf(buffer, 256, "%s\n\\#\\%d%%", gCurrLoadingSegment.str, (u8)floor(gCurrLoadingSegment.percentage * 100));
            } else {
                snprintf(buffer, 256, "%s...", gCurrLoadingSegment.str);
            }

            sys_swap_backslashes(buffer);
        }
        djui_text_set_text(sLoading->loadingDesc, buffer);
        djui_base_set_location(&sLoading->loadingDesc->base, 0, loadingDescY1);
    }

    // loading bar
#if defined(__3DS__)
    {
        f32 barY = loadingDescY2 + 24.0f;
        if (barY > windowHeight - 28.0f) {
            barY = windowHeight - 28.0f;
        }
        djui_base_set_location(&sLoading->loadingBar->base, windowWidth / 4, barY);
    }
#else
    djui_base_set_location(&sLoading->loadingBar->base, windowWidth / 4, loadingDescY2 + 64);
#endif
    djui_base_set_visible(&sLoading->loadingBar->base, gCurrLoadingSegment.percentage > 0 && strlen(gCurrLoadingSegment.str) > 0);

    djui_base_compute(base);

    MUTEX_UNLOCK(gLoadingThread);

    return true;
}

static void loading_screen_destroy(struct DjuiBase* base) {
    struct LoadingScreen* load = (struct LoadingScreen*)base;
    free(load);
    sLoading = NULL;
}

static void init_loading_screen(void) {
#if defined(__3DS__)
    new3ds_boot_checkpoint("loading: ui init");
#endif
    struct LoadingScreen* load = calloc(1, sizeof(struct LoadingScreen));
    struct DjuiBase* base = &load->base;

    djui_base_init(NULL, base, loading_screen_on_render, loading_screen_destroy);

    // splash text (easter egg)
    if (configExCoopTheme) {
        struct DjuiText* splashDjuiText = djui_text_create(base, "\\#ff0800\\SM\\#1be700\\64\\#00b3ff\\EX\n\\#ffef00\\COOP");
        djui_base_set_location_type(&splashDjuiText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_location(&splashDjuiText->base, 0, 0);
        djui_text_set_font(splashDjuiText, gDjuiFonts[1]);
        djui_text_set_font_scale(splashDjuiText, gDjuiFonts[1]->defaultFontScale);
        djui_text_set_alignment(splashDjuiText, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
        djui_base_set_size_type(&splashDjuiText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&splashDjuiText->base, 1.0f, gDjuiFonts[1]->defaultFontScale * 3.0f);

        load->splashText = splashDjuiText;

    // splash image
    } else {
        struct DjuiImage* splashImage = djui_image_create(base, texture_coopdx_logo, 2048, 1024, G_IM_FMT_RGBA, G_IM_SIZ_32b);
        djui_base_set_location_type(&splashImage->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_alignment(&splashImage->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
#if defined(__3DS__)
        /* Logo texture has a large white canvas — keep it compact on 400x240. */
        djui_base_set_location(&splashImage->base, 0, 8);
        djui_base_set_size(&splashImage->base, 280, 140);
#else
        djui_base_set_location(&splashImage->base, 0, -100);
        djui_base_set_size(&splashImage->base, 512, 256);
#endif

        load->splashImage = splashImage;
    }

    {
        // current loading stage text
        struct DjuiText *text = djui_text_create(base, "");
        djui_base_set_location_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_location(&text->base, 0, 0);

        djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text->base, 1.0f, gDjuiFonts[0]->defaultFontScale * 3.0f);
#if defined(__3DS__)
        /* High contrast on the dark clear (logo white wash no longer covers this). */
        djui_base_set_color(&text->base, 255, 255, 255, 255);
#else
        djui_base_set_color(&text->base, 220, 220, 220, 255);
#endif
        djui_text_set_alignment(text, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
        djui_text_set_font(text, gDjuiFonts[0]);
        djui_text_set_font_scale(text, gDjuiFonts[0]->defaultFontScale);

        load->loadingDesc = text;
    }

    {
        // loading bar
        struct DjuiProgressBar *progressBar = djui_progress_bar_create(base, &gCurrLoadingSegment.percentage, 0.0f, 1.0f, false);
        djui_base_set_location_type(&progressBar->base, DJUI_SVT_ABSOLUTE, DJUI_SVT_ABSOLUTE);
        djui_base_set_location(&progressBar->base, 0, 0);
        djui_base_set_visible(&progressBar->base, false);
        progressBar->base.width.value = 0.5;
        progressBar->smoothenHigh = 0.75f;
        progressBar->smoothenLow = 0.25f;

        load->loadingBar = progressBar;
    }

    sLoading = load;
}

void loading_screen_reset(void) {
#if defined(__3DS__)
    new3ds_boot_checkpoint("loading_reset: destroy ui");
#endif
    if (sLoading) {
        djui_base_destroy(&sLoading->base);
        sLoading = NULL;
    }
    djui_shutdown();
    alloc_display_list_reset();
    gDisplayListHead = NULL;
#if defined(__3DS__)
    new3ds_boot_checkpoint("loading_reset: rendering_init begin");
#endif
    rendering_init();
#if defined(__3DS__)
    new3ds_boot_checkpoint("loading_reset: rendering_init ok");
#endif
    configWindow.settings_changed = true;
}

void render_loading_screen(void) {
    if (!sLoading) { init_loading_screen(); }

#if defined(__3DS__)
    gfx_wm_set_force_exit_on_start(true);
    u32 loadingFrame = 0;
#endif

    // loading screen loop
    while (!gGameInited) {
#if defined(__3DS__)
        if (loadingFrame < 3) {
            char phase[48];
            snprintf(phase, sizeof(phase), "loading: frame %u", (unsigned int)(loadingFrame + 1));
            new3ds_boot_checkpoint(phase);
        }
        loadingFrame++;
#endif
        gfx_wm_main_loop(loading_screen_produce_one_frame);
    }

#if defined(__3DS__)
    gfx_wm_set_force_exit_on_start(false);
#endif

    int err = join_thread(&gLoadingThread);
    assert(err == 0);
}

void render_rom_setup_screen(void) {
#if defined(__3DS__)
    /*
     * Hardware cannot recover mid-boot without replacing the ROM on SD.
     * Show a clear bottom-screen fatal message instead of an infinite DJUI
     * loop that looks like a Homebrew hang.
     */
    const char *romError = rom_get_setup_error();
    char message[640];
    if (romError != NULL && romError[0] != '\0') {
        snprintf(
            message,
            sizeof(message),
            "%s\n\nDetails: sdmc:/3ds/sm64coopdx/logs/runtime.log\n\n"
            "Press START or HOME to exit.",
            romError);
    } else {
        snprintf(
            message,
            sizeof(message),
            "ERROR: baserom.us.z64 is missing.\n\n"
            "Place a vanilla US .z64 at:\n"
            "sdmc:/3ds/sm64coopdx/baserom.us.z64\n\n"
            "MD5 20b854b239203baf6c961b850a4a51a2\n\n"
            "Press START or HOME to exit.");
    }
    new3ds_boot_progress_end();
    new3ds_platform_show_exit_message(message);
    new3ds_platform_quit();
#else
    if (!sLoading) { init_loading_screen(); }

#if defined(__SWITCH__)
    {
        const char *romError = rom_get_setup_error();
        if (romError != NULL) {
            loading_screen_set_segment_text(romError);
        } else {
            loading_screen_set_segment_text(
                "No ROM found.\n"
                "Place baserom.us.z64 at:\n"
                "sdmc:/switch/sm64coopdx/\n"
                "Then restart the app.");
        }
    }
#else
    loading_screen_set_segment_text("No rom detected, drag & drop Super Mario 64 (U) [!].z64 on to this screen");
#endif

    while (!gRomIsValid) {
        gfx_wm_main_loop(loading_screen_produce_one_frame);
    }
#endif
}
