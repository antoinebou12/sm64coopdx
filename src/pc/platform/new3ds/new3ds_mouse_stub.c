#ifdef __3DS__

#include "pc/controller/controller_mouse.h"

bool mouse_init_ok = false;
u32 mouse_buttons = 0;
f32 mouse_x = 0;
f32 mouse_y = 0;
u32 mouse_window_buttons = 0;
u32 mouse_window_buttons_pressed = 0;
u32 mouse_window_buttons_released = 0;
f32 mouse_window_x = 0;
f32 mouse_window_y = 0;
u32 mouse_scroll_timestamp = 0;
f32 mouse_scroll_x = 0;
f32 mouse_scroll_y = 0;
bool mouse_relative_enabled = false;

void controller_mouse_read_window(void) {}
void controller_mouse_read_relative(void) {}
void controller_mouse_enter_relative(void) { mouse_relative_enabled = true; }
void controller_mouse_leave_relative(void) { mouse_relative_enabled = false; }
void mouse_on_scroll(float x, float y) { (void)x; (void)y; }

#endif
