#include "switch_coopnet_join_gate.h"

#include "pc/configfile.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_menu.h"
#include "pc/network/network.h"
#ifdef COOPNET
#include "pc/network/coopnet/coopnet.h"
#endif

static bool sPromptActive = false;
static bool sJoinReleased = false;
static unsigned int sGeneration = 1;

void __real_network_send_join_request(void);

void switch_coopnet_join_gate_reset(void) {
    sPromptActive = false;
    sJoinReleased = false;
    ++sGeneration;
    if (sGeneration == 0) { sGeneration = 1; }
}

static bool switch_coopnet_join_gate_is_active_session(void) {
#ifdef COOPNET
    return gNetworkType == NT_CLIENT && gNetworkSystem == &gNetworkSystemCoopNet;
#else
    return false;
#endif
}

static void switch_coopnet_join_gate_confirm(struct DjuiBase *caller) {
    if (!sPromptActive || caller == NULL || (unsigned int)caller->tag != sGeneration) { return; }
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }

    sPromptActive = false;
    sJoinReleased = true;
    configfile_save(configfile_name());
    djui_panel_back();

    if (!switch_coopnet_join_gate_is_active_session()) { return; }
    __real_network_send_join_request();
}

static void switch_coopnet_join_gate_cancel(struct DjuiBase *caller) {
    if (!sPromptActive || caller == NULL || (unsigned int)caller->tag != sGeneration) { return; }
    sPromptActive = false;
    ++sGeneration;
    network_reset_reconnect_and_rehost();
    network_shutdown(true, false, false, false);
    /* Character panel -> join-progress panel -> refreshed lobby panel. */
    djui_panel_back();
    djui_panel_back();
}

static bool switch_coopnet_join_gate_back(struct DjuiBase *caller) {
    switch_coopnet_join_gate_cancel(caller);
    return true;
}

static void switch_coopnet_join_gate_show(void) {
    if (sPromptActive) { return; }
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }

    sPromptActive = true;
    const unsigned int generation = sGeneration;
    struct DjuiThreePanel *panel = djui_panel_menu_create("Choose Character", true);
    struct DjuiBase *body = djui_three_panel_get_body(panel);

    char *characterChoices[CT_MAX] = { 0 };
    for (int i = 0; i < CT_MAX; ++i) { characterChoices[i] = gCharacters[i].name; }
    djui_selectionbox_create(body, "Character", characterChoices, CT_MAX, &configPlayerModel, NULL);

    struct DjuiButton *join = djui_button_create(body, "Join", DJUI_BUTTON_STYLE_NORMAL, switch_coopnet_join_gate_confirm);
    join->base.tag = (s64)generation;
    struct DjuiButton *cancel = djui_button_create(body, "Cancel", DJUI_BUTTON_STYLE_BACK, switch_coopnet_join_gate_cancel);
    cancel->base.tag = (s64)generation;
    panel->on_back = switch_coopnet_join_gate_back;

    if (djui_panel_add(NULL, panel, &join->base) == NULL) {
        sPromptActive = false;
        network_shutdown(true, false, true, false);
    }
}

void __wrap_network_send_join_request(void) {
    if (!switch_coopnet_join_gate_is_active_session()) {
        __real_network_send_join_request();
        return;
    }
    if (sJoinReleased) {
        __real_network_send_join_request();
        return;
    }
    switch_coopnet_join_gate_show();
}
