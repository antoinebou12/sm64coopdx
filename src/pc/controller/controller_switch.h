#pragma once

#include <PR/ultratypes.h>
#include <stdbool.h>

#include "controller_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct ControllerAPI controller_switch;

void controller_switch_poll(void);
bool controller_switch_read_slot(u8 slot, OSContPad *pad);
u8 controller_switch_connected_count(void);
bool controller_switch_rumble_slot(u8 slot, f32 strength);
void controller_switch_rumble_stop_slot(u8 slot);

#ifdef __cplusplus
}
#endif
