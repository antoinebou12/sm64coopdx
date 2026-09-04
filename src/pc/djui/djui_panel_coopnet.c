#if defined(__3DS__) && defined(COOPNET)

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_host.h"
#include "djui_panel_join_lobbies.h"
#include "djui_panel_join_private.h"
#include "djui_panel_coopnet.h"
#include "pc/configfile.h"
#include "pc/network/network.h"
#include "pc/platform/new3ds/new3ds_runtime.h"

static void djui_panel_coopnet_public(struct DjuiBase* caller) {
    djui_panel_join_lobbies_create(caller, "");
}

static void djui_panel_coopnet_host(struct DjuiBase* caller) {
    configNetworkSystem = NS_COOPNET;
    /* Warm SOC early so Host → Host does not fail on first juice socket. */
    (void)new3ds_runtime_ensure_network();
    djui_panel_host_create(caller);
}

void djui_panel_coopnet_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(HOST, COOPNET), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        struct DjuiText* tip = djui_text_create(
            body,
            "Internet (libjuice + CoopNet)\nsm64coop-android lobbies");
        djui_base_set_size_type(&tip->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&tip->base, 1.0f, 36);
        djui_base_set_color(&tip->base, 200, 200, 200, 255);
        djui_text_set_alignment(tip, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
        djui_text_set_font_scale(tip, tip->font->defaultFontScale * 0.65f);

        djui_button_create(body, "Join Public Lobbies", DJUI_BUTTON_STYLE_NORMAL, djui_panel_coopnet_public);
        djui_button_create(body, "Join Private Lobbies", DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_create);
        djui_button_create(body, "Host CoopNet Lobby", DJUI_BUTTON_STYLE_NORMAL, djui_panel_coopnet_host);
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}

#endif /* __3DS__ && COOPNET */
