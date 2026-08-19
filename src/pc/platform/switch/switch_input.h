#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWITCH_INPUT_MAX_PLAYERS 4

typedef enum SwitchInputButton {
    SWITCH_INPUT_A       = 1U << 0,
    SWITCH_INPUT_B       = 1U << 1,
    SWITCH_INPUT_X       = 1U << 2,
    SWITCH_INPUT_Y       = 1U << 3,
    SWITCH_INPUT_L       = 1U << 4,
    SWITCH_INPUT_R       = 1U << 5,
    SWITCH_INPUT_ZL      = 1U << 6,
    SWITCH_INPUT_ZR      = 1U << 7,
    SWITCH_INPUT_PLUS    = 1U << 8,
    SWITCH_INPUT_MINUS   = 1U << 9,
    SWITCH_INPUT_UP      = 1U << 10,
    SWITCH_INPUT_DOWN    = 1U << 11,
    SWITCH_INPUT_LEFT    = 1U << 12,
    SWITCH_INPUT_RIGHT   = 1U << 13,
    SWITCH_INPUT_STICK_L = 1U << 14,
    SWITCH_INPUT_STICK_R = 1U << 15,
} SwitchInputButton;

typedef enum SwitchInputStyle {
    SWITCH_INPUT_STYLE_NONE = 0,
    SWITCH_INPUT_STYLE_HANDHELD,
    SWITCH_INPUT_STYLE_PRO,
    SWITCH_INPUT_STYLE_JOY_DUAL,
    SWITCH_INPUT_STYLE_JOY_LEFT,
    SWITCH_INPUT_STYLE_JOY_RIGHT,
    SWITCH_INPUT_STYLE_GAMECUBE,
    SWITCH_INPUT_STYLE_N64,
    SWITCH_INPUT_STYLE_OTHER,
} SwitchInputStyle;

typedef struct SwitchInputPad {
    bool connected;
    SwitchInputStyle style;
    uint32_t buttons;
    uint32_t buttons_down;
    uint32_t buttons_up;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
} SwitchInputPad;

bool switch_input_init(void);
void switch_input_shutdown(void);
void switch_input_poll(void);

uint8_t switch_input_connected_count(void);
bool switch_input_get(uint8_t slot, SwitchInputPad *out);

bool switch_input_rumble(uint8_t slot, float strength);
void switch_input_rumble_stop(uint8_t slot);
void switch_input_rumble_stop_all(void);

#ifdef __cplusplus
}
#endif
