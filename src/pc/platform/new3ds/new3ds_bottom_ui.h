#pragma once

#ifdef __3DS__

#include <stdbool.h>

bool new3ds_bottom_ui_init(void);
void new3ds_bottom_ui_shutdown(void);
bool new3ds_bottom_ui_ready(void);
bool new3ds_bottom_ui_owns_console(void);
void new3ds_bottom_ui_poll_input(void);
/* Redraw log ring into the bottom framebuffer (call after C3D_FrameEnd). */
void new3ds_bottom_ui_draw(void);
bool new3ds_bottom_ui_touch_consumed(void);
/* Pause drawing while a system applet (e.g. experimental swkbd) owns the bottom screen. */
void new3ds_bottom_ui_set_paused(bool paused);
void new3ds_bottom_ui_reinit_console(void);

#endif /* __3DS__ */
