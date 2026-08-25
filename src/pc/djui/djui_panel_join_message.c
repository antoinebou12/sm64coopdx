#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_main.h"
#include "djui_panel_join_lobbies.h"
#include "djui_panel_join_message.h"
#include "djui_popup.h"
#include "game/characters.h"
#include "pc/network/network.h"
#include "pc/network/coopnet/coopnet.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"
#ifdef __SWITCH__
#include "pc/platform/switch/switch_coopnet_log.h"
#include "pc/platform/switch/switch_crash_log.h"
#endif

#define DJUI_JOIN_MESSAGE_ELAPSE 60
bool gDjuiPanelJoinMessageVisible = false;
float gDownloadProgress = 0;
float gDownloadProgressInf = 0;
char gDownloadEstimate[DOWNLOAD_ESTIMATE_LENGTH] = "";

static struct DjuiText* sPanelText = NULL;
static bool sDisplayingError = false;
static bool sProgrammaticReturn = false;

#if defined(__SWITCH__) && defined(COOPNET)
static bool sCharacterPromptActive = false;

static void djui_panel_join_character_destroy(UNUSED struct DjuiBase* caller) {
    sCharacterPromptActive = false;
}

static void djui_panel_join_character_cancel(UNUSED struct DjuiBase* caller) {
    if (!sCharacterPromptActive) { return; }
    sCharacterPromptActive = false;
    switch_coopnet_log_printf("character selection cancelled");
    switch_crash_log_checkpoint("network: character selection cancelled");
    network_reset_reconnect_and_rehost();
    network_shutdown(true, false, false, false);
}

static bool djui_panel_join_character_back(struct DjuiBase* caller) {
    djui_panel_join_character_cancel(caller);
    return true;
}

static void djui_panel_join_character_continue(struct DjuiBase* caller) {
    if (!sCharacterPromptActive || gNetworkType != NT_CLIENT ||
        gNetworkSystem != &gNetworkSystemCoopNet || gNetworkSentJoin) {
        return;
    }

    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }
    configPlayerModelSelected = true;
    configfile_save(configfile_name());
    sCharacterPromptActive = false;
    switch_coopnet_log_printf("character selection confirmed model=%u", configPlayerModel);
    switch_crash_log_checkpoint("network: character selection confirmed");

    /* Restore the joining panel before sending. A fast join response may tear
     * down the entire panel stack immediately, so no UI state is accessed
     * after network_send_join_request(). */
    djui_panel_menu_back(caller);
    network_send_join_request();
}

static void djui_panel_join_character_create(void) {
    if (sCharacterPromptActive) { return; }
    sCharacterPromptActive = true;
    switch_coopnet_log_printf("character selection requested current_model=%u", configPlayerModel);
    switch_crash_log_checkpoint("network: character selection requested");

    struct DjuiThreePanel* panel = djui_panel_menu_create("SELECT CHARACTER", true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    char* characterChoices[CT_MAX] = { 0 };
    for (int i = 0; i < CT_MAX; i++) {
        characterChoices[i] = gCharacters[i].name;
    }
    djui_selectionbox_create(body, DLANG(PLAYER, MODEL), characterChoices,
                             CT_MAX, &configPlayerModel, NULL);

    struct DjuiRect* buttons = djui_rect_container_create(body, 64);
    djui_button_left_create(&buttons->base, DLANG(MENU, CANCEL),
                            DJUI_BUTTON_STYLE_BACK, djui_panel_join_character_cancel);
    struct DjuiButton* continueButton = djui_button_right_create(
        &buttons->base, "Continue", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_join_character_continue);

    panel->on_back = djui_panel_join_character_back;
    struct DjuiPanel* added = djui_panel_add(NULL, panel, &continueButton->base);
    if (added == NULL) {
        sCharacterPromptActive = false;
        network_shutdown(true, false, false, false);
        return;
    }
    added->on_panel_destroy = djui_panel_join_character_destroy;
}
#endif


void djui_panel_join_message_error(const char* message) {
    djui_panel_join_message_create(NULL);
    sDisplayingError = true;
    djui_text_set_text(sPanelText, message);
}

void djui_panel_join_message_return_to_lobbies(const char* message) {
    if (message != NULL && message[0] != '\0') {
        djui_popup_create((char*)message, 4);
    }

    if (gDjuiPanelJoinMessageVisible) {
        sProgrammaticReturn = true;
        djui_panel_back();
        /* djui_panel_back invokes the panel callback synchronously. Clear the
         * bypass here too in case a transition was already in progress. */
        sProgrammaticReturn = false;
    }
    djui_panel_join_lobbies_refresh(NULL);
}

void djui_panel_join_message_ready_to_join(void) {
    if (gNetworkType != NT_CLIENT || gNetworkSentJoin) { return; }

#if defined(__SWITCH__) && defined(COOPNET)
    if (gNetworkSystem == &gNetworkSystemCoopNet && !configPlayerModelSelected) {
        djui_panel_join_character_create();
        return;
    }
#endif

    network_send_join_request();
}

void djui_panel_join_message_cancel(struct DjuiBase* caller) {
    if (network_is_reconnecting()) { return; }
    network_reset_reconnect_and_rehost();
    network_shutdown(true, false, false, false);
    djui_panel_menu_back(caller);
}

bool djui_panel_join_message_back(struct DjuiBase* caller) {
    if (sProgrammaticReturn) {
        sProgrammaticReturn = false;
        return false;
    }
    djui_panel_join_message_cancel(caller);
    return true;
}

void djui_panel_join_message_render_pre(struct DjuiBase* base, UNUSED bool* unused) {
    if (sDisplayingError) { return; }
    struct DjuiText* text1 = (struct DjuiText*)base;
    u16 lastElapse = (base->tag / DJUI_JOIN_MESSAGE_ELAPSE);
    base->tag = (base->tag + 1) % (DJUI_JOIN_MESSAGE_ELAPSE * 3);
    u16 elapse = (base->tag / DJUI_JOIN_MESSAGE_ELAPSE);
    if (lastElapse != elapse) {
        char tmp[DOWNLOAD_ESTIMATE_LENGTH + 4] = "";
        switch (base->tag / DJUI_JOIN_MESSAGE_ELAPSE) {
            case 0:  snprintf(tmp, DOWNLOAD_ESTIMATE_LENGTH + 4, "%s\n...", gDownloadEstimate); break;
            case 1:  snprintf(tmp, DOWNLOAD_ESTIMATE_LENGTH + 4, "%s\n.",   gDownloadEstimate); break;
            default: snprintf(tmp, DOWNLOAD_ESTIMATE_LENGTH + 4, "%s\n..",  gDownloadEstimate); break;
        }
        djui_text_set_text(text1, tmp);
    }
}

void djui_panel_join_message_create(struct DjuiBase* caller) {
    // make sure main panel was created
    if (!gDjuiPanelMainCreated) { djui_panel_main_create(caller); }

    // don't recreate panel if it's already visible
    if (gDjuiPanelJoinMessageVisible) { return; }

    sProgrammaticReturn = false;

    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(JOIN_MESSAGE, JOINING), true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        snprintf(gDownloadEstimate, 32, " ");
        struct DjuiText* text1 = djui_text_create(body, "\n...");
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, 32 * 4);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);
        djui_text_set_alignment(text1, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
        text1->base.tag = 0;
        text1->base.on_render_pre = djui_panel_join_message_render_pre;
        sPanelText = text1;

        gDownloadProgressInf = 0;
        djui_progress_bar_create(body, &gDownloadProgressInf, 0.0f, 1.0f, true);

        gDownloadProgress = 0;
        djui_progress_bar_create(body, &gDownloadProgress, 0.0f, 1.0f, false);

        djui_button_create(body, DLANG(MENU, CANCEL), DJUI_BUTTON_STYLE_BACK, djui_panel_join_message_cancel);
    }
    panel->on_back = djui_panel_join_message_back;

    djui_panel_add(caller, panel, NULL);
    gDjuiPanelJoinMessageVisible = true;
    sDisplayingError = false;
}
