#ifdef __SWITCH__

#include <stdint.h>
#include <stdio.h>
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "pc/network/network.h"
#include "pc/network/socket/socket_ldn.h"
#include "pc/configfile.h"
#include "pc/debuglog.h"

static struct DjuiFlowLayout* sRoomLayout = NULL;
static bool sBusy = false;
static bool sKeepLdnAlive = false;

// Plain-C boundary from socket_ldn.c. Closing discovery when the user backs
// out is important because an open LDN station can interfere with returning
// to normal infrastructure/direct-IP networking.
extern void ldn_shutdown_impl(void);
extern void djui_panel_do_host(bool reconnecting, bool playSound);

static void djui_panel_ldn_refresh(UNUSED struct DjuiBase* caller);

static void djui_panel_ldn_connect(struct DjuiBase* caller) {
    if (sBusy) { return; }
    sBusy = true;

    s32 index = (s32)(intptr_t)caller->tag;
    LOG_INFO("LDN: joining room %d", index);

    // Associate first. network_init then sees an already-connected LDN
    // backend and can initialize normal CoopDX client state without a window
    // where gNetworkType says client while association is still failing.
    if (!ldn_connect_to_index(index)) {
        djui_popup_create("Could not join the local wireless room.\nKeep both consoles in the Homebrew Menu\napplication-memory mode and try again.", 3);
        sBusy = false;
        return;
    }

    network_reset_reconnect_and_rehost();
    network_set_system(NS_LDN);
    if (!network_init(NT_CLIENT, false)) {
        gNetworkSystem->shutdown(false);
        djui_popup_create("Local wireless connected, but CoopDX\nnetwork initialization failed.", 3);
        sBusy = false;
        return;
    }

    sKeepLdnAlive = true;
    djui_connect_menu_open();
    network_send_mod_list_request();
    sBusy = false;
}

static void djui_panel_ldn_host(UNUSED struct DjuiBase* caller) {
    if (sBusy) { return; }
    sBusy = true;

    // Let the standard host lifecycle do save-slot setup, mod activation,
    // fake-level initialization, transitions and future rehosts. The Switch
    // network overlay teaches network_set_system() how to resolve NS_LDN.
    network_reset_reconnect_and_rehost();
    configNetworkSystem = NS_LDN;

    // djui_panel_do_host() tears the menu down before it calls network_init(),
    // so this panel's on_destroy runs first. Hand the association over to the
    // host lifecycle instead of letting the teardown close it, and release the
    // busy latch here because this function does not return to the panel.
    sKeepLdnAlive = true;
    sBusy = false;
    djui_panel_do_host(false, true);
}

static void djui_panel_ldn_rebuild_rooms(void) {
    if (sRoomLayout == NULL) { return; }
    djui_base_destroy_children(&sRoomLayout->base);

    s32 count = ldn_get_network_count();
    if (count <= 0) {
        struct DjuiText* text = djui_text_create(&sRoomLayout->base, "NO LOCAL WIRELESS ROOMS FOUND");
        djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text->base, 1.0f, 48);
        djui_text_set_alignment(text, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
        return;
    }

    // LDN backend currently advertises at most four rooms per scan.
    for (s32 i = 0; i < count; i++) {
        char label[96];
        snprintf(label, sizeof(label), "%s  (%d/%d)",
                 ldn_get_network_name(i),
                 ldn_get_network_player_count(i),
                 ldn_get_network_max_players(i));
        struct DjuiButton* button = djui_button_create(&sRoomLayout->base, label, DJUI_BUTTON_STYLE_NORMAL, djui_panel_ldn_connect);
        button->base.tag = (s64)(intptr_t)i;
    }
}

static void djui_panel_ldn_refresh(UNUSED struct DjuiBase* caller) {
    if (sBusy) { return; }
    sBusy = true;
    bool ok = ldn_refresh_scan();
    LOG_INFO("LDN: scan %s, rooms=%d", ok ? "ok" : "failed", ldn_get_network_count());
    djui_panel_ldn_rebuild_rooms();
    if (!ok) {
        djui_popup_create("Local wireless scan failed.", 2);
    }
    sBusy = false;
}

static void djui_panel_ldn_on_destroy(UNUSED struct DjuiBase* caller) {
    sRoomLayout = NULL;
    sBusy = false;
    if (!sKeepLdnAlive) {
        ldn_shutdown_impl();
    }
    sKeepLdnAlive = false;
}

void djui_panel_ldn_browser_create(struct DjuiBase* caller) {
    sKeepLdnAlive = false;

    struct DjuiThreePanel* panel = djui_panel_menu_create("LOCAL WIRELESS", true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "HOST LOCAL ROOM", DJUI_BUTTON_STYLE_NORMAL, djui_panel_ldn_host);

    sRoomLayout = djui_flow_layout_create(body);
    djui_base_set_size_type(&sRoomLayout->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&sRoomLayout->base, 1.0f, 240);
    djui_flow_layout_set_margin(sRoomLayout, 4);

    djui_button_create(body, "REFRESH ROOMS", DJUI_BUTTON_STYLE_NORMAL, djui_panel_ldn_refresh);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);

    struct DjuiPanel* p = djui_panel_add(caller, panel, NULL);
    if (p != NULL) {
        p->on_panel_destroy = djui_panel_ldn_on_destroy;
    }

    djui_panel_ldn_refresh(NULL);
}

#else

void djui_panel_ldn_browser_create(struct DjuiBase* caller) {
    (void)caller;
}

#endif
