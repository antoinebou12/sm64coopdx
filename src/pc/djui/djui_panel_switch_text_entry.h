#ifndef DJUI_PANEL_SWITCH_TEXT_ENTRY_H
#define DJUI_PANEL_SWITCH_TEXT_ENTRY_H

#if defined(__SWITCH__) || defined(__3DS__)

#include "djui.h"

enum DjuiSwitchTextEntryMode {
    DJUI_SWITCH_TEXT_NUMERIC,
    DJUI_SWITCH_TEXT_ADDRESS,
    DJUI_SWITCH_TEXT_PASSWORD,
};

typedef void (*DjuiSwitchTextEntryAccept)(const char* text);

void djui_panel_switch_text_entry_create(
    struct DjuiBase* caller,
    const char* title,
    const char* initialText,
    u16 capacity,
    enum DjuiSwitchTextEntryMode mode,
    DjuiSwitchTextEntryAccept onAccept);

#endif

#endif
