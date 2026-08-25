#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_join_lobbies.h"
#include "djui_panel_join_private.h"
#include "djui_panel_join_direct.h"
#include "pc/network/network.h"
#include "pc/utils/misc.h"

#ifdef COOPNET

#define PRIVATE_PASSWORD_CAPACITY 64

#ifdef __SWITCH__
#define PRIVATE_KEY_COUNT 26

static char sPrivatePassword[PRIVATE_PASSWORD_CAPACITY] = "";
static struct DjuiText* sPasswordText = NULL;
static struct DjuiButton* sLetterButtons[PRIVATE_KEY_COUNT] = { 0 };
static bool sUppercase = false;

static void djui_panel_join_private_update_password(void) {
    if (!sPasswordText) { return; }

    char masked[PRIVATE_PASSWORD_CAPACITY] = "";
    size_t length = strlen(sPrivatePassword);
    memset(masked, '#', length);
    masked[length] = '\0';
    djui_text_set_text(sPasswordText, length > 0 ? masked : "(empty)");
}

static void djui_panel_join_private_key(struct DjuiBase* caller) {
    size_t length = strlen(sPrivatePassword);
    if (length + 1 >= sizeof(sPrivatePassword)) { return; }

    char character = (char)caller->tag;
    if (!sUppercase && character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 'a');
    }
    sPrivatePassword[length] = character;
    sPrivatePassword[length + 1] = '\0';
    djui_panel_join_private_update_password();
}

static void djui_panel_join_private_backspace(UNUSED struct DjuiBase* caller) {
    size_t length = strlen(sPrivatePassword);
    if (length > 0) {
        sPrivatePassword[length - 1] = '\0';
        djui_panel_join_private_update_password();
    }
}

static void djui_panel_join_private_clear(UNUSED struct DjuiBase* caller) {
    sPrivatePassword[0] = '\0';
    djui_panel_join_private_update_password();
}

static void djui_panel_join_private_case(UNUSED struct DjuiBase* caller) {
    sUppercase = !sUppercase;
    for (int i = 0; i < PRIVATE_KEY_COUNT; i++) {
        if (!sLetterButtons[i]) { continue; }
        char label[2] = { (char)((sUppercase ? 'A' : 'a') + i), '\0' };
        djui_text_set_text(sLetterButtons[i]->text, label);
    }
}

static struct DjuiButton* djui_panel_join_private_key_row(
    struct DjuiBase* body,
    const char* characters,
    struct DjuiButton* defaultButton) {
    size_t count = strlen(characters);
    struct DjuiRect* row = djui_rect_container_create(body, 44);
    for (size_t i = 0; i < count; i++) {
        char label[2] = { characters[i], '\0' };
        if (!sUppercase && label[0] >= 'A' && label[0] <= 'Z') {
            label[0] = (char)(label[0] - 'A' + 'a');
        }
        struct DjuiButton* button = djui_button_create(
            &row->base, label, DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_key);
        button->base.tag = (s64)characters[i];
        djui_base_set_size_type(&button->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&button->base, (1.0f / (f32)count) - 0.006f, 42);
        djui_base_set_location_type(&button->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_location(&button->base, (f32)i / (f32)count, 0);
        djui_base_set_alignment(&button->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

        if (characters[i] >= 'A' && characters[i] <= 'Z') {
            sLetterButtons[characters[i] - 'A'] = button;
        }
        if (!defaultButton) { defaultButton = button; }
    }
    return defaultButton;
}

static void djui_panel_join_private_on_destroy(UNUSED struct DjuiBase* caller) {
    sPasswordText = NULL;
    memset(sLetterButtons, 0, sizeof(sLetterButtons));
}
#else
static struct DjuiInputbox* sInputboxPassword = NULL;
#endif

static void djui_panel_join_private_lobbies(struct DjuiBase* caller) {
#ifdef __SWITCH__
    djui_panel_join_lobbies_create(caller, sPrivatePassword);
#else
    djui_panel_join_lobbies_create(caller, sInputboxPassword->buffer);
#endif
}

void djui_panel_join_private_create(struct DjuiBase* caller) {
    struct DjuiBase* defaultBase = NULL;
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(LOBBIES, PRIVATE_LOBBIES), true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        struct DjuiText* text1 = djui_text_create(body, DLANG(LOBBIES, ENTER_PASSWORD));
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, 100);
        djui_base_compute_tree(&text1->base);
        u16 directLines = djui_text_count_lines(text1, 12);
        f32 directTextHeight = 32 * 0.8125f * directLines + 8;
        djui_base_set_size(&text1->base, 1.0f, directTextHeight);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);

#ifdef __SWITCH__
        struct DjuiText* passwordText = djui_text_create(body, "(empty)");
        djui_base_set_size_type(&passwordText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&passwordText->base, 1.0f, 42);
        djui_base_set_color(&passwordText->base, 220, 220, 220, 255);
        djui_text_set_alignment(passwordText, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
        djui_text_set_drop_shadow(passwordText, 64, 64, 64, 100);
        sPasswordText = passwordText;
        djui_panel_join_private_update_password();

        struct DjuiButton* defaultButton = NULL;
        defaultButton = djui_panel_join_private_key_row(body, "1234567890", defaultButton);
        defaultButton = djui_panel_join_private_key_row(body, "QWERTYUIOP", defaultButton);
        defaultButton = djui_panel_join_private_key_row(body, "ASDFGHJKL", defaultButton);
        defaultButton = djui_panel_join_private_key_row(body, "ZXCVBNM", defaultButton);
        defaultButton = djui_panel_join_private_key_row(body, "-_.!@#$%", defaultButton);
        if (defaultButton) { defaultBase = &defaultButton->base; }

        struct DjuiRect* editRow = djui_rect_container_create(body, 50);
        struct DjuiButton* caseButton = djui_button_create(
            &editRow->base, "a/A", DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_case);
        djui_base_set_size(&caseButton->base, 0.32f, 48);
        djui_base_set_alignment(&caseButton->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
        struct DjuiButton* backspaceButton = djui_button_create(
            &editRow->base, "Backspace", DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_backspace);
        djui_base_set_size(&backspaceButton->base, 0.32f, 48);
        djui_base_set_alignment(&backspaceButton->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
        struct DjuiButton* clearButton = djui_button_create(
            &editRow->base, "Clear", DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_clear);
        djui_base_set_size(&clearButton->base, 0.32f, 48);
        djui_base_set_alignment(&clearButton->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
#else
        struct DjuiInputbox* inputbox1 = djui_inputbox_create(body, PRIVATE_PASSWORD_CAPACITY);
        inputbox1->passwordChar[0] = '#';
        djui_base_set_size_type(&inputbox1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&inputbox1->base, 1.0f, 32.0f);
        sInputboxPassword = inputbox1;
#endif

        struct DjuiRect* rect2 = djui_rect_container_create(body, 64);
        {
            struct DjuiButton* button1 = djui_button_create(&rect2->base, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
            djui_base_set_size(&button1->base, 0.485f, 64);
            djui_base_set_alignment(&button1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

            struct DjuiButton* button2 = djui_button_create(&rect2->base, DLANG(LOBBIES, SEARCH), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_private_lobbies);
            djui_base_set_size(&button2->base, 0.485f, 64);
            djui_base_set_alignment(&button2->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
#ifndef __SWITCH__
            defaultBase = &button2->base;
#endif
        }
    }

    struct DjuiPanel* added = djui_panel_add(caller, panel, defaultBase);
#ifdef __SWITCH__
    if (added) {
        added->on_panel_destroy = djui_panel_join_private_on_destroy;
    }
#endif
}

#endif
