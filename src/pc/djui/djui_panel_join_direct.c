#include <stdio.h>
#include <string.h>
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
#if defined(__SWITCH__) || defined(__3DS__)
#include "djui_panel_switch_text_entry.h"
#endif
#if defined(__3DS__)
#include "pc/platform/new3ds/new3ds_runtime.h"
#endif

#if !defined(__SWITCH__) && !defined(__3DS__)
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

#if !defined(__SWITCH__) && !defined(__3DS__)
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

    /* Trim leading/trailing whitespace from keypad/saved config. */
    {
        char* start = buffer;
        while (*start == ' ' || *start == '\t') { start++; }
        if (start != buffer) {
            memmove(buffer, start, strlen(start) + 1);
        }
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == ' ' || buffer[len - 1] == '\t')) {
            buffer[len - 1] = '\0';
            len--;
        }
    }

    memcpy(&orig_ip, &buffer, sizeof(orig_ip));

    bool afterSpacer = false;
    bool is_ipv6 = false;
    int port = 0;

#if !defined(__SWITCH__) && !defined(__3DS__)
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

    snprintf(gGetHostName, MAX_CONFIG_STRING, "%s", orig_ip[0] != '\0' ? orig_ip : buffer);
    if (snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", buffer) < 0) {
        LOG_INFO("truncating IP");
    }
    /* Keep hostname alias in sync for socket client post-resolve overwrite. */
    if (gGetHostName[0] == '\0') {
        snprintf(gGetHostName, MAX_CONFIG_STRING, "%s", configJoinIp);
    }
    if (port >= 1 && port <= 65535) {
        configJoinPort = port;
    } else {
        configJoinPort = DEFAULT_PORT;
    }
}

#if defined(__3DS__)
static void djui_panel_join_direct_ensure_default_address(void) {
    /* Never invent a LAN IP — a wrong default makes Join look "broken". */
    if (strcmp(configJoinIp, "localhost") == 0) {
        configJoinIp[0] = '\0';
    }
    if (configJoinPort == 0) {
        configJoinPort = DEFAULT_PORT;
    }
}
#endif

static void djui_panel_join_direct_format_address(char* buffer, size_t bufferSize) {
#if defined(__3DS__)
    djui_panel_join_direct_ensure_default_address();
    if (configJoinIp[0] != '\0') {
        snprintf(buffer, bufferSize, "%s:%u", configJoinIp, configJoinPort);
    } else {
        snprintf(buffer, bufferSize, "0.0.0.0:%u", configJoinPort);
    }
#else
    if (strlen(configJoinIp) > 0 && configJoinPort != DEFAULT_PORT) {
        snprintf(buffer, bufferSize, "%s:%d", configJoinIp, configJoinPort);
    } else if (strlen(configJoinIp) > 0) {
        snprintf(buffer, bufferSize, "%s", configJoinIp);
    } else {
        snprintf(buffer, bufferSize, "localhost");
    }
#endif
}

#if !defined(__SWITCH__) && !defined(__3DS__)
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
#if !defined(__SWITCH__) && !defined(__3DS__)
    if (!(strlen(sInputboxIp->buffer) > 2)) {
        djui_interactable_set_input_focus(&sInputboxIp->base);
        djui_inputbox_select_all(sInputboxIp);
        return;
    }
    djui_panel_join_direct_ip_text_set_new(sInputboxIp->buffer);
#else
    if (sSwitchDirectAddress[0] == '\0') {
        djui_panel_join_direct_format_address(sSwitchDirectAddress, sizeof(sSwitchDirectAddress));
    }
    djui_panel_join_direct_ip_text_set_new(sSwitchDirectAddress);
#if defined(__3DS__)
    djui_panel_join_direct_ensure_default_address();
    if (configJoinIp[0] == '\0'
        || strcmp(configJoinIp, "0.0.0.0") == 0
        || strcmp(configJoinIp, "enter.ip.here") == 0) {
        djui_popup_create("Enter the host IPv4 address first\n(example: 192.168.1.10:1234).", 3);
        return;
    }
#endif
    if (strlen(configJoinIp) <= 2) {
#if defined(__3DS__)
        djui_popup_create("Enter a valid host IPv4 address.", 2);
#endif
        return;
    }
    LOG_INFO("Direct: join begin host=%s port=%u", configJoinIp, configJoinPort);
#endif
#if defined(__3DS__)
    configNetworkSystem = NS_SOCKET;
    if (!new3ds_runtime_ensure_network() || !new3ds_runtime_network_available()) {
        LOG_ERROR("New 3DS Direct: SOC unavailable for join");
        djui_popup_create("Network unavailable.\nEnable Wi-Fi, then try Join again.", 3);
        return;
    }
#endif
    network_reset_reconnect_and_rehost();
    network_set_system(NS_SOCKET);
    if (!network_init(NT_CLIENT, false)) {
#if defined(__SWITCH__) || defined(__3DS__)
        LOG_ERROR("Direct: network_init failed host=%s port=%u", configJoinIp, configJoinPort);
#endif
#if defined(__3DS__)
        djui_popup_create("Could not start LAN join.\nCheck the IP/port and Wi-Fi.", 3);
#endif
        return;
    }
    djui_panel_join_message_create(caller);
}

void djui_panel_join_direct_create(struct DjuiBase* caller) {
    struct DjuiBase* defaultBase = NULL;
#if defined(__3DS__)
    configNetworkSystem = NS_SOCKET;
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(JOIN, DIRECT), false);
#else
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(JOIN, JOIN_TITLE), false);
#endif
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
#if defined(__3DS__)
        /* Short plain lines — long JOIN_SOCKET + color codes overlap on 400x240. */
        {
            struct DjuiText* title = djui_text_create(body, "Enter host LAN IP");
            djui_base_set_size_type(&title->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&title->base, 1.0f, 26);
            djui_base_set_color(&title->base, 230, 230, 230, 255);
            djui_text_set_alignment(title, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
            djui_text_set_font_scale(title, title->font->defaultFontScale * 0.55f);

            struct DjuiText* tip = djui_text_create(body, "Ex: 192.168.1.10:1234");
            djui_base_set_size_type(&tip->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&tip->base, 1.0f, 24);
            djui_base_set_color(&tip->base, 160, 210, 160, 255);
            djui_text_set_alignment(tip, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
            djui_text_set_font_scale(tip, tip->font->defaultFontScale * 0.5f);
        }
#else
        struct DjuiText* text1 = djui_text_create(body, DLANG(JOIN, JOIN_SOCKET));
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, 100);
        djui_base_compute_tree(&text1->base);
        u16 directLines = djui_text_count_lines(text1, 12);
        f32 directTextHeight = 32 * 0.8125f * directLines + 8;
        djui_base_set_size(&text1->base, 1.0f, directTextHeight);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);
#endif

#if !defined(__SWITCH__) && !defined(__3DS__)
        struct DjuiInputbox* inputbox1 = djui_inputbox_create(body, 256);
        djui_base_set_size_type(&inputbox1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&inputbox1->base, 1.0f, 32.0f);
        djui_interactable_hook_value_change(&inputbox1->base, djui_panel_join_direct_ip_text_change);
        sInputboxIp = inputbox1;
        djui_panel_join_direct_ip_text_set(inputbox1);
#else
        djui_panel_join_direct_format_address(sSwitchDirectAddress, sizeof(sSwitchDirectAddress));
        if (configJoinIp[0] != '\0' && strcmp(configJoinIp, "0.0.0.0") != 0) {
            djui_panel_join_direct_ip_text_set_new(sSwitchDirectAddress);
        }
        sButtonIp = djui_button_create(body, sSwitchDirectAddress, DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_direct_switch_edit);
        djui_base_set_size_type(&sButtonIp->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
#if defined(__3DS__)
        djui_base_set_size(&sButtonIp->base, 1.0f, 36.0f);
#else
        djui_base_set_size(&sButtonIp->base, 1.0f, 48.0f);
#endif
#endif

#if defined(__3DS__)
        /* Keep Back/Join fully inside the padded body so the right button is not clipped. */
        const f32 footerH = 48.0f;
        struct DjuiRect* rect2 = djui_rect_container_create(body, footerH);
        djui_base_set_padding(&rect2->base, 0, 4, 0, 4);
        {
            struct DjuiButton* button1 = djui_button_left_create(
                &rect2->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
            struct DjuiButton* button2 = djui_button_right_create(
                &rect2->base, DLANG(JOIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_direct_do_join);
            djui_base_set_size(&button1->base, 0.46f, footerH);
            djui_base_set_size(&button2->base, 0.46f, footerH);
            defaultBase = &button2->base;
        }
#else
        const f32 footerH = 64.0f;
        struct DjuiRect* rect2 = djui_rect_container_create(body, footerH);
        {
            struct DjuiButton* button1 = djui_button_create(&rect2->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
            djui_base_set_size(&button1->base, 0.485f, footerH);
            djui_base_set_alignment(&button1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

            struct DjuiButton* button2 = djui_button_create(&rect2->base, DLANG(JOIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_direct_do_join);
            djui_base_set_size(&button2->base, 0.485f, footerH);
            djui_base_set_alignment(&button2->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
            defaultBase = &button2->base;
        }
#endif
    }

    djui_panel_add(caller, panel, defaultBase);
}
