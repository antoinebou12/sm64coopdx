#if defined(__SWITCH__) || defined(__3DS__)

#include <stdio.h>
#include <string.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_switch_text_entry.h"

#define SWITCH_TEXT_BUFFER_CAPACITY 256
#define SWITCH_TEXT_LETTER_COUNT 26

static char sTextBuffer[SWITCH_TEXT_BUFFER_CAPACITY] = "";
static u16 sTextCapacity = SWITCH_TEXT_BUFFER_CAPACITY;
static enum DjuiSwitchTextEntryMode sTextMode = DJUI_SWITCH_TEXT_ADDRESS;
static DjuiSwitchTextEntryAccept sOnAccept = NULL;
static struct DjuiText* sValueText = NULL;
static struct DjuiButton* sLetterButtons[SWITCH_TEXT_LETTER_COUNT] = { 0 };
static bool sUppercase = false;

static void switch_text_update_display(void) {
    if (sValueText == NULL) { return; }

    if (sTextMode == DJUI_SWITCH_TEXT_PASSWORD) {
        char masked[SWITCH_TEXT_BUFFER_CAPACITY] = "";
        const size_t length = strlen(sTextBuffer);
        memset(masked, '#', length);
        masked[length] = '\0';
        djui_text_set_text(sValueText, length > 0 ? masked : "(empty)");
    } else {
        djui_text_set_text(sValueText, sTextBuffer[0] != '\0' ? sTextBuffer : "(empty)");
    }
}

static bool switch_text_character_allowed(char character) {
    if (character >= '0' && character <= '9') { return true; }
    if (sTextMode == DJUI_SWITCH_TEXT_NUMERIC) { return false; }
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) { return true; }
    if (sTextMode == DJUI_SWITCH_TEXT_ADDRESS) {
        return character == '.' || character == ':' || character == '-' || character == '_';
    }
    return character == '-' || character == '_' || character == '.' || character == '!'
        || character == '@' || character == '#' || character == '$' || character == '%';
}

static void switch_text_key(struct DjuiBase* caller) {
    size_t length = strlen(sTextBuffer);
    if (length + 1 >= sTextCapacity || length + 1 >= sizeof(sTextBuffer)) { return; }

    char character = (char)caller->tag;
    if (!sUppercase && character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 'a');
    }
    if (!switch_text_character_allowed(character)) { return; }

    sTextBuffer[length] = character;
    sTextBuffer[length + 1] = '\0';
    switch_text_update_display();
}

static void switch_text_backspace(UNUSED struct DjuiBase* caller) {
    const size_t length = strlen(sTextBuffer);
    if (length > 0) {
        sTextBuffer[length - 1] = '\0';
        switch_text_update_display();
    }
}

static void switch_text_clear(UNUSED struct DjuiBase* caller) {
    sTextBuffer[0] = '\0';
    switch_text_update_display();
}

static void switch_text_case(UNUSED struct DjuiBase* caller) {
    sUppercase = !sUppercase;
    for (int i = 0; i < SWITCH_TEXT_LETTER_COUNT; i++) {
        if (sLetterButtons[i] == NULL) { continue; }
        char label[2] = { (char)((sUppercase ? 'A' : 'a') + i), '\0' };
        djui_text_set_text(sLetterButtons[i]->text, label);
    }
}

static struct DjuiButton* switch_text_key_row(
    struct DjuiBase* body,
    const char* characters,
    struct DjuiButton* defaultButton) {
    const size_t count = strlen(characters);
    if (count == 0) { return defaultButton; }

#if defined(__3DS__)
    const f32 rowH = 28.0f;
    const f32 keyH = 26.0f;
#else
    const f32 rowH = 44.0f;
    const f32 keyH = 42.0f;
#endif
    struct DjuiRect* row = djui_rect_container_create(body, rowH);
    for (size_t i = 0; i < count; i++) {
        char label[2] = { characters[i], '\0' };
        if (!sUppercase && label[0] >= 'A' && label[0] <= 'Z') {
            label[0] = (char)(label[0] - 'A' + 'a');
        }
        struct DjuiButton* button = djui_button_create(
            &row->base, label, DJUI_BUTTON_STYLE_NORMAL, switch_text_key);
        button->base.tag = (s64)characters[i];
        djui_base_set_size_type(&button->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&button->base, (1.0f / (f32)count) - 0.006f, keyH);
        djui_base_set_location_type(&button->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_location(&button->base, (f32)i / (f32)count, 0);
        djui_base_set_alignment(&button->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

        if (characters[i] >= 'A' && characters[i] <= 'Z') {
            sLetterButtons[characters[i] - 'A'] = button;
        }
        if (defaultButton == NULL) { defaultButton = button; }
    }
    return defaultButton;
}

static void switch_text_accept(UNUSED struct DjuiBase* caller) {
    char accepted[SWITCH_TEXT_BUFFER_CAPACITY];
    snprintf(accepted, sizeof(accepted), "%s", sTextBuffer);
    DjuiSwitchTextEntryAccept callback = sOnAccept;
    if (callback != NULL) {
        callback(accepted);
    }
    djui_panel_back();
}

static void switch_text_on_destroy(UNUSED struct DjuiBase* caller) {
    memset(sLetterButtons, 0, sizeof(sLetterButtons));
    sValueText = NULL;
    sOnAccept = NULL;
    if (sTextMode == DJUI_SWITCH_TEXT_PASSWORD) {
        memset(sTextBuffer, 0, sizeof(sTextBuffer));
    }
}

void djui_panel_switch_text_entry_create(
    struct DjuiBase* caller,
    const char* title,
    const char* initialText,
    u16 capacity,
    enum DjuiSwitchTextEntryMode mode,
    DjuiSwitchTextEntryAccept onAccept) {
    memset(sLetterButtons, 0, sizeof(sLetterButtons));
    sUppercase = false;
    sTextMode = mode;
    sOnAccept = onAccept;
    sTextCapacity = capacity;
    if (sTextCapacity < 2) { sTextCapacity = 2; }
    if (sTextCapacity > sizeof(sTextBuffer)) { sTextCapacity = sizeof(sTextBuffer); }
    snprintf(sTextBuffer, sTextCapacity, "%s", initialText != NULL ? initialText : "");

    struct DjuiThreePanel* panel = djui_panel_menu_create(title != NULL ? title : "Edit", true);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    sValueText = djui_text_create(body, "");
    djui_base_set_size_type(&sValueText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
#if defined(__3DS__)
    djui_base_set_size(&sValueText->base, 1.0f, 28);
#else
    djui_base_set_size(&sValueText->base, 1.0f, 48);
#endif
    djui_base_set_color(&sValueText->base, 220, 220, 220, 255);
    djui_text_set_alignment(sValueText, DJUI_HALIGN_CENTER, DJUI_VALIGN_CENTER);
    djui_text_set_drop_shadow(sValueText, 64, 64, 64, 100);
    switch_text_update_display();

    struct DjuiButton* defaultButton = NULL;
    defaultButton = switch_text_key_row(body, "1234567890", defaultButton);
    if (mode != DJUI_SWITCH_TEXT_NUMERIC) {
        defaultButton = switch_text_key_row(body, "QWERTYUIOP", defaultButton);
        defaultButton = switch_text_key_row(body, "ASDFGHJKL", defaultButton);
        defaultButton = switch_text_key_row(body, "ZXCVBNM", defaultButton);
        defaultButton = switch_text_key_row(
            body,
            mode == DJUI_SWITCH_TEXT_ADDRESS ? ".:-_" : "-_.!@#$%",
            defaultButton);
    }

#if defined(__3DS__)
    const f32 editH = 32.0f;
    const f32 editBtnH = 30.0f;
    const f32 actionH = 36.0f;
#else
    const f32 editH = 50.0f;
    const f32 editBtnH = 48.0f;
    const f32 actionH = 64.0f;
#endif
    struct DjuiRect* editRow = djui_rect_container_create(body, editH);
    if (mode != DJUI_SWITCH_TEXT_NUMERIC) {
        struct DjuiButton* caseButton = djui_button_create(
            &editRow->base, "a/A", DJUI_BUTTON_STYLE_NORMAL, switch_text_case);
        djui_base_set_size(&caseButton->base, 0.32f, editBtnH);
        djui_base_set_alignment(&caseButton->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
    }
    struct DjuiButton* backspaceButton = djui_button_create(
        &editRow->base, "Backspace", DJUI_BUTTON_STYLE_NORMAL, switch_text_backspace);
    djui_base_set_size(&backspaceButton->base, mode == DJUI_SWITCH_TEXT_NUMERIC ? 0.49f : 0.32f, editBtnH);
    djui_base_set_alignment(&backspaceButton->base,
                            mode == DJUI_SWITCH_TEXT_NUMERIC ? DJUI_HALIGN_LEFT : DJUI_HALIGN_CENTER,
                            DJUI_VALIGN_TOP);
    struct DjuiButton* clearButton = djui_button_create(
        &editRow->base, "Clear", DJUI_BUTTON_STYLE_NORMAL, switch_text_clear);
    djui_base_set_size(&clearButton->base, mode == DJUI_SWITCH_TEXT_NUMERIC ? 0.49f : 0.32f, editBtnH);
    djui_base_set_alignment(&clearButton->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);

    struct DjuiRect* actionRow = djui_rect_container_create(body, actionH);
    struct DjuiButton* backButton = djui_button_create(
        &actionRow->base, "Back", DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_base_set_size(&backButton->base, 0.485f, actionH);
    djui_base_set_alignment(&backButton->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
    struct DjuiButton* acceptButton = djui_button_create(
        &actionRow->base, "Apply", DJUI_BUTTON_STYLE_NORMAL, switch_text_accept);
    djui_base_set_size(&acceptButton->base, 0.485f, actionH);
    djui_base_set_alignment(&acceptButton->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);

    struct DjuiPanel* added = djui_panel_add(caller, panel, defaultButton != NULL ? &defaultButton->base : &acceptButton->base);
    if (added != NULL) {
        added->on_panel_destroy = switch_text_on_destroy;
    }
}

#endif
