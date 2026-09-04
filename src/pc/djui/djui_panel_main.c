#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_host.h"
#include "djui_panel_join.h"
#include "djui_panel_options.h"
#include "djui_panel_menu.h"
#include "djui_panel_confirm.h"
#include "pc/controller/controller_sdl.h"
#include "pc/pc_main.h"
#include "pc/update_checker.h"
#include "pc/configfile.h"
#include "pc/network/network.h"
#if defined(__3DS__) && defined(COOPNET)
#include "djui_panel_coopnet.h"
#endif

extern ALIGNED8 u8 texture_coopdx_logo[];

bool gDjuiPanelMainCreated = false;

static void djui_panel_main_quit_yes(UNUSED struct DjuiBase* caller) {
    game_exit();
}

static void djui_panel_main_quit(struct DjuiBase* caller) {
    djui_panel_confirm_create(caller,
                              DLANG(MAIN, QUIT_TITLE),
                              DLANG(MAIN, QUIT_CONFIRM),
                              djui_panel_main_quit_yes);
}

#if defined(__3DS__)
static void djui_panel_main_host_lan(struct DjuiBase* caller) {
    /* Main Host is always LAN/direct — CoopNet has its own menu entry. */
    configNetworkSystem = NS_SOCKET;
    djui_panel_host_create(caller);
}
#endif

void djui_panel_main_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(configExCoopTheme ? "\\#ff0800\\SM\\#1be700\\64\\#00b3ff\\EX\n\\#ffef00\\COOP" : "", false);
    {
        struct DjuiBase* body = djui_three_panel_get_body(panel);
        {
            if (!configExCoopTheme) {
                struct DjuiImage* logo = djui_image_create(body, texture_coopdx_logo, 2048, 1024, G_IM_FMT_RGBA, G_IM_SIZ_32b);
#if defined(__3DS__)
                /* Compact logo so Host/Join/CoopNet/Options/Quit stay on-screen. */
                djui_base_set_size(&logo->base, 120, 60);
                djui_base_set_alignment(&logo->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
                djui_base_set_location_type(&logo->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
                djui_base_set_location(&logo->base, 0, -4);
#elif defined(HANDHELD)
                djui_base_set_size(&logo->base, configDjuiThemeCenter ? 400 : 360, configDjuiThemeCenter ? 200 : 180);
                djui_base_set_alignment(&logo->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
                djui_base_set_location_type(&logo->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
                djui_base_set_location(&logo->base, 0, -30);
#else
                if (configDjuiThemeCenter) {
                    djui_base_set_size(&logo->base, 550, 275);
                } else {
                    djui_base_set_size(&logo->base, 480, 240);
                }
                djui_base_set_alignment(&logo->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
                djui_base_set_location_type(&logo->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
                djui_base_set_location(&logo->base, 0, -30);
#endif
            }

#if defined(__3DS__)
            struct DjuiButton* button1 = djui_button_create(body, "Host LAN", DJUI_BUTTON_STYLE_NORMAL, djui_panel_main_host_lan);
            if (!configExCoopTheme) { djui_base_set_location(&button1->base, 0, -4); }
#else
            struct DjuiButton* button1 = djui_button_create(body, DLANG(MAIN, HOST), DJUI_BUTTON_STYLE_NORMAL, djui_panel_host_create);
            if (!configExCoopTheme) { djui_base_set_location(&button1->base, 0, -30); }
#endif
            djui_cursor_input_controlled_center(&button1->base);

            struct DjuiButton* button2 = djui_button_create(body, DLANG(MAIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_create);
#if defined(__3DS__)
            if (!configExCoopTheme) { djui_base_set_location(&button2->base, 0, -4); }
#else
            if (!configExCoopTheme) { djui_base_set_location(&button2->base, 0, -30); }
#endif
#if defined(__3DS__) && defined(COOPNET)
            /* Explicit label — CoopNet is separate from Host LAN / Mod Dev Mode. */
            struct DjuiButton* buttonCoop = djui_button_create(body, "CoopNet", DJUI_BUTTON_STYLE_NORMAL, djui_panel_coopnet_create);
            if (!configExCoopTheme) { djui_base_set_location(&buttonCoop->base, 0, -4); }
#endif
            struct DjuiButton* button3 = djui_button_create(body, DLANG(MAIN, OPTIONS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_options_create);
#if defined(__3DS__)
            if (!configExCoopTheme) { djui_base_set_location(&button3->base, 0, -4); }
#else
            if (!configExCoopTheme) { djui_base_set_location(&button3->base, 0, -30); }
#endif
            struct DjuiButton* button4 = djui_button_create(body, DLANG(MAIN, QUIT), DJUI_BUTTON_STYLE_BACK, djui_panel_main_quit);
#if defined(__3DS__)
            if (!configExCoopTheme) { djui_base_set_location(&button4->base, 0, -4); }
#else
            if (!configExCoopTheme) { djui_base_set_location(&button4->base, 0, -30); }
#endif
        }

        // these two cannot co-exist for some reason
        if (gUpdateMessage) {
            struct DjuiText* message = djui_text_create(&panel->base, DLANG(NOTIF, UPDATE_AVAILABLE));
            djui_base_set_size_type(&message->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&message->base, 1.0f, 1.0f);
            djui_base_set_color(&message->base, 255, 255, 160, 255);
            djui_text_set_alignment(message, DJUI_HALIGN_CENTER, DJUI_VALIGN_BOTTOM);
        } else {
            struct DjuiText* version = djui_text_create(
                &panel->base,
                #ifdef COMPILE_TIME
                    get_version_with_build_date()
                #else
                    get_version()
                #endif
            );
            djui_base_set_size_type(&version->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&version->base, 1.0f, 1.0f);
            djui_base_set_color(&version->base, 50, 50, 50, 255);
            djui_text_set_alignment(version, configExCoopTheme ? DJUI_HALIGN_CENTER : DJUI_HALIGN_RIGHT, DJUI_VALIGN_BOTTOM);
        }
    }

    djui_panel_add(caller, panel, NULL);
    gInteractableOverridePad = true;
    gDjuiPanelMainCreated = true;
}
