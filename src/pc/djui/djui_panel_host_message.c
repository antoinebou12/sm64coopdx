#include <stdio.h>
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_modlist.h"
#include "pc/network/network.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"
#include "pc/utils/misc.h"
#include "game/level_update.h"
#include "game/hardcoded.h"
#include "game/area.h"
#include "engine/math_util.h"
#include "audio/external.h"
#include "sounds.h"
#ifdef __SWITCH__
#include "pc/platform/switch/switch_crash_log.h"
#endif
#ifdef __3DS__
#include "pc/platform/new3ds/new3ds_log.h"
#include "pc/platform/new3ds/new3ds_runtime.h"
#endif

static bool hideMessage = false;

void djui_panel_do_host(bool reconnecting, bool playSound) {
#ifdef __SWITCH__
    switch_crash_log_printf(
        "host begin backend=%u players=%u port=%u reconnecting=%d",
        configNetworkSystem,
        configAmountOfPlayers,
        configHostPort,
        reconnecting ? 1 : 0);
    switch_crash_log_checkpoint("host: begin");
#endif
#ifdef __3DS__
    NEW3DS_LOG_INFO_CAT(
        NEW3DS_LOG_CAT_NET,
        "host",
        "begin backend=%u players=%u port=%u reconnecting=%d",
        configNetworkSystem,
        configAmountOfPlayers,
        configHostPort,
        reconnecting ? 1 : 0);
    if (!new3ds_runtime_network_available()) {
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_NET, "host", "blocked: SOC unavailable");
        djui_popup_create("Network unavailable. Restart the game or check wireless settings.", 4.0f);
        return;
    }
#endif

    stop_demo(NULL);
    djui_panel_shutdown();
    extern s16 gCurrSaveFileNum;
    gCurrSaveFileNum = configHostSaveSlot;
    update_all_mario_stars();

#ifndef COOPNET
    if (configNetworkSystem == NS_COOPNET) { configNetworkSystem = NS_SOCKET; }
#endif
    if (configNetworkSystem == NS_COOPNET && configAmountOfPlayers == 1) { configNetworkSystem = NS_SOCKET; }
    // NS_MAX is the sentinel, not a usable backend: clamping to it lands in
    // the default branch of network_set_system() and logs an error.
    if (configNetworkSystem >= NS_MAX) { configNetworkSystem = NS_SOCKET; }
    network_set_system(configNetworkSystem);

#ifdef __SWITCH__
    switch_crash_log_checkpoint("host: network init begin");
#endif
#ifdef __3DS__
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "host", "network init begin");
#endif
    if (!network_init(NT_SERVER, reconnecting)) {
#ifdef __SWITCH__
        switch_crash_log_checkpoint("host: network init failed");
#endif
#ifdef __3DS__
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_NET, "host", "network init failed");
#endif
        // network_init() can fail after the host panel has already been closed.
        // Use the standard shutdown path to clear any partial backend state and
        // rebuild the main menu instead of continuing into a half-started game.
        network_shutdown(false, false, true, false);
        return;
    }
#ifdef __SWITCH__
    switch_crash_log_checkpoint("host: network init complete");
#endif
#ifdef __3DS__
    {
        char ip[32];
        new3ds_runtime_get_ipv4_string(ip, sizeof(ip));
        NEW3DS_LOG_INFO_CAT(
            NEW3DS_LOG_CAT_NET,
            "host",
            "network init complete clients_join_at=%s:%u",
            ip,
            configHostPort);
    }
#endif

    djui_panel_modlist_create(NULL);
#ifdef __SWITCH__
    switch_crash_log_checkpoint("host: modlist complete");
#endif
#ifdef __3DS__
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "host", "modlist complete");
#endif
    fake_lvl_init_from_save_file();
#ifdef __SWITCH__
    switch_crash_log_checkpoint("host: level init complete");
#endif
#ifdef __3DS__
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "host", "level init complete");
#endif

    extern s16 gChangeLevelTransition;
    gChangeLevelTransition = gLevelValues.entryLevel;

    if (gMarioState->marioObj) vec3f_copy(gMarioState->marioObj->header.gfx.cameraToObject, gGlobalSoundSource);
    if (playSound) { gDelayedInitSound = CHAR_SOUND_OKEY_DOKEY; }

    play_transition(WARP_TRANSITION_FADE_INTO_STAR, 0x14, 0x00, 0x00, 0x00);
#ifdef __SWITCH__
    switch_crash_log_checkpoint("host: transition started");
#endif
#ifdef __3DS__
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_NET, "host", "transition started");
#endif
}

void djui_panel_host_message_do_host(UNUSED struct DjuiBase* caller) {
    if (hideMessage) { configHideSocketWarning = true; }
    network_reset_reconnect_and_rehost();
    djui_panel_do_host(false, true);
}

void djui_panel_host_message_create(struct DjuiBase* caller) {
    char* warningMessage = NULL;
    bool hideHostButton = false;

    f32 warningLines = 8;
    warningMessage = calloc(512, sizeof(char));
#ifdef __3DS__
    {
        char ip[32];
        const bool networkReady = new3ds_runtime_get_ipv4_string(ip, sizeof(ip));
        if (!networkReady) {
            snprintf(
                warningMessage,
                512,
                "Network is offline (SOC unavailable).\n\n"
                "Enable wireless in System Settings, restart the game, then try hosting again.");
            hideHostButton = true;
            warningLines = 6;
        } else {
            snprintf(
                warningMessage,
                512,
                "Clients join at %s:%u\n\n%s",
                ip,
                configHostPort,
                DLANG(HOST_MESSAGE, WARN_SOCKET));
        }
    }
#else
    snprintf(warningMessage, 512, DLANG(HOST_MESSAGE, WARN_SOCKET), configHostPort);
#endif

    f32 textHeight = 32 * 0.8125f * warningLines + 8;

    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(HOST_MESSAGE, INFO_TITLE), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        struct DjuiText* text1 = djui_text_create(body, warningMessage);
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, textHeight);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);
        djui_text_set_drop_shadow(text1, 64, 64, 64, 100);

        struct DjuiCheckbox* chkHide = djui_checkbox_create(body, DLANG(HOST_MESSAGE, WARN_SOCKET_HIDE), &hideMessage, NULL);

        struct DjuiRect* rect1 = djui_rect_container_create(body, 64);
        {
            struct DjuiButton* btnHost = djui_button_right_create(&rect1->base, DLANG(HOST_MESSAGE, HOST), DJUI_BUTTON_STYLE_NORMAL, djui_panel_host_message_do_host);
            struct DjuiButton* btnBack = djui_button_left_create(&rect1->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);

            if (hideHostButton) {
                djui_base_set_size(&btnBack->base, 1.0f, 64);
                djui_base_set_visible(&btnHost->base, false);
                djui_base_set_enabled(&btnHost->base, false);
                djui_base_set_visible(&chkHide->base, false);
                djui_base_set_enabled(&chkHide->base, false);
            }
        }
    }

    djui_panel_add(caller, panel, NULL);
    free(warningMessage);
}
