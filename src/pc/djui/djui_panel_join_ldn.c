#ifdef __SWITCH__

#include <stdio.h>

#include "djui.h"
#include "djui_lobby_entry.h"
#include "djui_panel.h"
#include "djui_panel_join_ldn.h"
#include "djui_panel_join_message.h"
#include "djui_panel_menu.h"
#include "pc/network/network.h"
#include "pc/network/socket/socket_ldn.h"

static struct DjuiPaginated* sPages;
static struct DjuiFlowLayout* sLayout;
static struct DjuiButton* sRefresh;

static void join_selected(struct DjuiBase* caller) {
    if (caller == NULL) return;

    network_reset_reconnect_and_rehost();
    network_ldn_select();
    if (!network_ldn_connect_to_index((int)caller->tag)) {
        djui_popup_create("Could not join local wireless game", 2);
        return;
    }

    configNetworkSystem = NS_LDN;
    if (!network_init(NT_CLIENT, false)) {
        djui_popup_create("Local wireless connection failed", 2);
        return;
    }
    djui_panel_join_message_create(caller);
}

static void finish_refresh(void) {
    if (sLayout == NULL || sPages == NULL || sRefresh == NULL) return;
    if (sLayout->base.child == NULL) {
        struct DjuiText* text = djui_text_create(&sLayout->base, "No local wireless games found");
        djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_RELATIVE);
        djui_base_set_size(&text->base, 1, 1);
        djui_text_set_alignment(text, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
    }
    djui_text_set_text(sRefresh->text, DLANG(LOBBIES, REFRESH));
    djui_base_set_enabled(&sRefresh->base, true);
    djui_paginated_update_page_buttons(sPages);
}

static void refresh_list(UNUSED struct DjuiBase* caller) {
    if (sLayout == NULL || sPages == NULL || sRefresh == NULL) return;
    djui_base_destroy_children(&sLayout->base);
    djui_text_set_text(sRefresh->text, DLANG(LOBBIES, REFRESHING));
    djui_base_set_enabled(&sRefresh->base, false);

    if (network_ldn_refresh_scan()) {
        for (int i = 0; i < network_ldn_network_count(); i++) {
            char players[32];
            int current = network_ldn_network_player_count(i);
            int maximum = network_ldn_network_max_players(i);
            snprintf(players, sizeof(players), "%d/%d", current, maximum);

            struct DjuiLobbyEntry* entry = djui_lobby_entry_create(
                &sLayout->base,
                (char*)network_ldn_network_name(i),
                "Local Wireless",
                players,
                "Nearby Nintendo Switch session",
                current >= maximum,
                join_selected,
                NULL,
                NULL);
            entry->base.tag = i;
        }
    }
    finish_refresh();
}

static void destroy_panel(UNUSED struct DjuiBase* caller) {
    sRefresh = NULL;
    sLayout = NULL;
    sPages = NULL;
    network_ldn_cancel_scan();
}

void djui_panel_join_ldn_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Local Wireless", true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    sPages = djui_paginated_create(body, 10);
    sLayout = sPages->layout;
    djui_flow_layout_set_margin(sLayout, 4);

    struct DjuiRect* actions = djui_rect_container_create(body, 64);
    struct DjuiButton* back = djui_button_create(
        &actions->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_base_set_size(&back->base, 0.485f, 64);
    djui_base_set_alignment(&back->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

    sRefresh = djui_button_create(
        &actions->base, DLANG(LOBBIES, REFRESH), DJUI_BUTTON_STYLE_NORMAL, refresh_list);
    djui_base_set_size(&sRefresh->base, 0.485f, 64);
    djui_base_set_alignment(&sRefresh->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);

    struct DjuiPanel* added = djui_panel_add(caller, panel, &sRefresh->base);
    if (added == NULL) return;
    added->on_panel_destroy = destroy_panel;
    refresh_list(NULL);
}

#endif
