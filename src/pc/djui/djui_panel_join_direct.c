#include <stdio.h>
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_modlist.h"
#include "djui_panel_join_message.h"
#include "djui_lobby_entry.h"
#include "pc/network/network.h"
#include "pc/network/socket/socket.h"
#include "pc/network/coopnet/coopnet.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"
#include "pc/debuglog.h"
#include "macros.h"
#ifdef __SWITCH__
#include "djui_panel_switch_text_entry.h"
#endif

#ifndef __SWITCH__
static struct DjuiInputbox* sInputboxIp = NULL;
#else
static struct DjuiButton* sButtonIp = NULL;
static char sSwitchDirectAddress[256] = "";
#endif

static bool djui_panel_join_direct_ip_parse_numbers(char** msg) {
    int num = 0;
    for (int i = 0; i < 3; i++) {
        char c = **msg;
        if (c >= '0' && c <= '9') {
            num *= 10;
            num += (c - '0');
            *msg = *msg + 1;
        } else if (i == 0) {
            return false;
        } else {
            break;
        }
    }

    return num >= 0 && num <= 255;
}

static bool djui_panel_join_direct_ip_parse_period(char** msg) {
    char c = **msg;
    bool isPeriod = (c == '.');
    if (isPeriod) { *msg = *msg + 1; }
    return isPeriod;
}

static bool djui_panel_join_direct_ip_parse_spacer(char** msg) {
    char c = **msg;
    bool isSpacer = (c == ':' || c == ' ');
    if (isSpacer) { *msg = *msg + 1; }
    return isSpacer;
}

static bool djui_panel_join_direct_ip_parse_port(char** msg) {
    int port = 0;
    for (int i = 0; i < 5; i++) {
        char c = **msg;
        if (c >= '0' && c <= '9') {
            port *= 10;
            port += (c - '0');
            *msg = *msg + 1;
        } else if (i == 0) {
            return false;
        } else {
            break;
        }
    }

    return port <= 65535;
}

UNUSED static bool djui_panel_join_direct_ip_valid(char* buffer) {
    char** msg = &buffer;

    if (!djui_panel_join_direct_ip_parse_numbers(msg)) { return false; }
    if (!djui_panel_join_direct_ip_parse_period(msg))  { return false; }
    if (!djui_panel_join_direct_ip_parse_numbers(msg)) { return false; }
    if (!djui_panel_join_direct_ip_parse_period(msg))  { return false; }
    if (!djui_panel_join_direct_ip_parse_numbers(msg)) { return false; }
    if (!djui_panel_join_direct_ip_parse_period(msg))  { return false; }
    if (!djui_panel_join_direct_ip_parse_numbers(msg)) { return false; }
    if (djui_panel_join_direct_ip_parse_spacer(msg)) {
        if (!djui_panel_join_direct_ip_parse_port(msg)) { return false; }
    }

    return (**msg == '\0');
}

#ifndef __SWITCH__
static void djui_panel_join_direct_ip_text_change(struct DjuiBase* caller) {
    struct DjuiInputbox* inputbox1 = (struct DjuiInputbox*)caller;
    struct DjuiTheme* theme = gDjuiThemes[configDjuiTheme];
    struct DjuiColor* textColor = &theme->interactables.textColor;
    if (strlen(inputbox1->buffer) > 2) {
        djui_inputbox_set_text_color(inputbox1, textColor->r, textColor->g, textColor->b, textColor->a);
    } else {
        djui_inputbox_set_text_color(inputbox1, 255, 0, 0, 255);
    }
}
#endif

static void djui_panel_join_direct_ip_text_set_new(const char* source) {
    char buffer[256] = { 0 };
    char orig_ip[256] = { 0 };
    if (snprintf(buffer, sizeof(buffer), "%s", source != NULL ? source : "") < 0) {
        LOG_INFO("truncating IP");
    }

    memcpy(&orig_ip, &buffer, sizeof(orig_ip));

    bool afterSpacer = false;
    bool is_ipv6 = false;
    int port = 0;

#ifndef __SWITCH__
    if (buffer[0] == '[') {
        memcpy(&buffer, &buffer[1], 255);
        is_ipv6 = true;
    }
#endif

    if (is_ipv6) {
        LOG_INFO("Detected direct IPv6 address");
    } else {
        LOG_INFO("Detected direct IPv4 address or hostname");
    }

    for (int i = 0; i < 256; i++) {
        if (is_ipv6 == true) {
            if ((buffer[i] == ']') || buffer[i] == ' ') {
                afterSpacer = true;
                memset(&orig_ip, 0, sizeof(orig_ip));
                memcpy(&orig_ip[1], &buffer, i + 1);
                buffer[i] = '\0';
                orig_ip[0] = '[';
                if (buffer[i + 1] == ':') {
                    i += 1;
                }
            } else if (buffer[i] == '\0') {
                break;
            } else if (afterSpacer && buffer[i] >= '0' && buffer[i] <= '9') {
                port *= 10;
                port += buffer[i] - '0';
            }
        } else {
            if (buffer[i] == ' ' || buffer[i] == ':') {
                afterSpacer = true;
                buffer[i] = '\0';
                memcpy(&orig_ip, &buffer, i + 1);
            } else if (buffer[i] == '\0') {
                break;
            } else if (afterSpacer && buffer[i] >= '0' && buffer[i] <= '9') {
                port *= 10;
                port += buffer[i] - '0';
            }
        }
    }

    snprintf(gGetHostName, MAX_CONFIG_STRING, "%s", orig_ip);
    if (snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", buffer) < 0) {
        LOG_INFO("truncating IP");
    }
    if (port >= 1 && port <= 65535) {
        configJoinPort = port;
    } else {
        configJoinPort = DEFAULT_PORT;
    }
}

static void djui_panel_join_direct_format_address(char* buffer, size_t bufferSize) {
    if (strlen(configJoinIp) > 0 && configJoinPort != DEFAULT_PORT) {
        snprintf(buffer, bufferSize, "%s:%d", configJoinIp, configJoinPort);
    } else if (strlen(configJoinIp) > 0) {
        snprintf(buffer, bufferSize, "%s", configJoinIp);
    } else {
        snprintf(buffer, bufferSize, "localhost");
    }
}

#ifndef __SWITCH__
static void djui_panel_join_direct_ip_text_set(struct DjuiInputbox* inputbox1) {
    char buffer[256] = { 0 };
    djui_panel_join_direct_format_address(buffer, sizeof(buffer));
    djui_inputbox_set_text(inputbox1, buffer);
}
#else
static void djui_panel_join_direct_switch_apply(const char* text) {
    if (text == NULL || strlen(text) <= 2) { return; }
    snprintf(sSwitchDirectAddress, sizeof(sSwitchDirectAddress), "%s", text);
    djui_panel_join_direct_ip_text_set_new(sSwitchDirectAddress);
    if (sButtonIp != NULL) {
        djui_text_set_text(sButtonIp->text, sSwitchDirectAddress);
    }
}

static void djui_panel_join_direct_switch_edit(struct DjuiBase* caller) {
    if (sSwitchDirectAddress[0] == '\0') {
        djui_panel_join_direct_format_address(sSwitchDirectAddress, sizeof(sSwitchDirectAddress));
    }
    djui_panel_switch_text_entry_create(
        caller,
        "Direct address",
        sSwitchDirectAddress,
        sizeof(sSwitchDirectAddress),
        DJUI_SWITCH_TEXT_ADDRESS,
        djui_panel_join_direct_switch_apply);
}
#endif

void djui_panel_join_direct_do_join(struct DjuiBase* caller) {
#ifndef __SWITCH__
    if (!(strlen(sInputboxIp->buffer) > 2)) {
        djui_interactable_set_input_focus(&sInputboxIp->base);
        djui_inputbox_select_all(sInputboxIp);
        return;
    }
    djui_panel_join_direct_ip_text_set_new(sInputboxIp->buffer);
#else
    if (sSwitchDirectAddress[0] == '\0') {
        djui_panel_join_direct_format_address(sSwitchDirectAddress, sizeof(sSwitchDirectAddress));
        djui_panel_join_direct_ip_text_set_new(sSwitchDirectAddress);
    }
    if (strlen(configJoinIp) <= 2) { return; }
    LOG_INFO("Switch Direct: join begin host=%s port=%u", configJoinIp, configJoinPort);
#endif
    network_reset_reconnect_and_rehost();
    network_set_system(NS_SOCKET);
    if (!network_init(NT_CLIENT, false)) {
#ifdef __SWITCH__
        LOG_ERROR("Switch Direct: network_init failed host=%s port=%u", configJoinIp, configJoinPort);
#endif
        return;
    }
    djui_panel_join_message_create(caller);
}

void djui_panel_join_direct_create(struct DjuiBase* caller) {
    struct DjuiBase* defaultBase = NULL;
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(JOIN, JOIN_TITLE), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        struct DjuiText* text1 = djui_text_create(body, DLANG(JOIN, JOIN_SOCKET));
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, 100);
        djui_base_compute_tree(&text1->base);
        u16 directLines = djui_text_count_lines(text1, 12);
        f32 directTextHeight = 32 * 0.8125f * directLines + 8;
        djui_base_set_size(&text1->base, 1.0f, directTextHeight);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);

#ifndef __SWITCH__
        struct DjuiInputbox* inputbox1 = djui_inputbox_create(body, 256);
        djui_base_set_size_type(&inputbox1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&inputbox1->base, 1.0f, 32.0f);
        djui_interactable_hook_value_change(&inputbox1->base, djui_panel_join_direct_ip_text_change);
        sInputboxIp = inputbox1;
        djui_panel_join_direct_ip_text_set(inputbox1);
#else
        djui_panel_join_direct_format_address(sSwitchDirectAddress, sizeof(sSwitchDirectAddress));
        sButtonIp = djui_button_create(body, sSwitchDirectAddress, DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_direct_switch_edit);
        djui_base_set_size_type(&sButtonIp->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&sButtonIp->base, 1.0f, 48.0f);
#endif

        struct DjuiRect* rect2 = djui_rect_container_create(body, 64);
        {
            struct DjuiButton* button1 = djui_button_create(&rect2->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
            djui_base_set_size(&button1->base, 0.485f, 64);
            djui_base_set_alignment(&button1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

            struct DjuiButton* button2 = djui_button_create(&rect2->base, DLANG(JOIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_direct_do_join);
            djui_base_set_size(&button2->base, 0.485f, 64);
            djui_base_set_alignment(&button2->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
            defaultBase = &button2->base;
        }
    }

    djui_panel_add(caller, panel, defaultBase);
}
