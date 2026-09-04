#ifdef __3DS__

#include "pc/djui/djui.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_menu.h"
#include "pc/configfile.h"
#include "macros.h"

static void new3ds_controls_note(struct DjuiBase *body, const char *message, float height) {
    struct DjuiText *text = djui_text_create(body, message);
    djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&text->base, 1.0f, height);
    djui_base_set_color(&text->base, 235, 235, 235, 255);
    djui_text_set_font_scale(text, text->font->defaultFontScale * 0.65f);
}

static void new3ds_controls_value_change(UNUSED struct DjuiBase *caller) {
    /* Native HID reads these settings directly every frame. */
}

static void new3ds_controls_stick_options_create(struct DjuiBase *caller) {
    struct DjuiThreePanel *panel = djui_panel_menu_create(DLANG(CONTROLS, ANALOG_STICK_OPTIONS), false);
    struct DjuiBase *body = djui_three_panel_get_body(panel);

    new3ds_controls_note(body,
        "Circle Pad = move   C-Stick = camera",
        28.0f);

    djui_checkbox_create(body, DLANG(CONTROLS, ROTATE_LEFT), &configStick.rotateLeft, NULL);
    djui_checkbox_create(body, DLANG(CONTROLS, INVERT_LEFT_X), &configStick.invertLeftX, NULL);
    djui_checkbox_create(body, DLANG(CONTROLS, INVERT_LEFT_Y), &configStick.invertLeftY, NULL);
    djui_checkbox_create(body, DLANG(CONTROLS, ROTATE_RIGHT), &configStick.rotateRight, NULL);
    djui_checkbox_create(body, DLANG(CONTROLS, INVERT_RIGHT_X), &configStick.invertRightX, NULL);
    djui_checkbox_create(body, DLANG(CONTROLS, INVERT_RIGHT_Y), &configStick.invertRightY, NULL);
    djui_slider_create(body, DLANG(CONTROLS, DEADZONE), &configStickDeadzone, 0, 100, new3ds_controls_value_change);

    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

void djui_panel_controls_create(struct DjuiBase *caller) {
    struct DjuiThreePanel *panel = djui_panel_menu_create(DLANG(CONTROLS, CONTROLS), false);
    struct DjuiBase *body = djui_three_panel_get_body(panel);

    new3ds_controls_note(body,
        "NEW 3DS BUILT-IN CONTROLS\n"
        "Circle Pad move  C-Stick camera\n"
        "A/B/X/Y actions  L/R/ZL/ZR shoulders\n"
        "D-Pad menu  C-Stick scroll lists  Touch cursor",
        72.0f);

    djui_button_create(body, DLANG(CONTROLS, ANALOG_STICK_OPTIONS), DJUI_BUTTON_STYLE_NORMAL, new3ds_controls_stick_options_create);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);

    djui_panel_add(caller, panel, NULL);
}

void djui_panel_controls_refresh_binds(UNUSED struct DjuiBase *parent) {
    /* The New 3DS uses a fixed native HID map rather than desktop bind widgets. */
}

#endif /* __3DS__ */
