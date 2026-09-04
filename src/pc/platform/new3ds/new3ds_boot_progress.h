#pragma once

#ifdef __3DS__

#include <stdbool.h>

/* Early bottom-screen console so Homebrew Launcher releases and SD waits are visible. */
void new3ds_boot_progress_begin(void);
void new3ds_boot_progress_set(const char *phase);
void new3ds_boot_progress_pump(void);
void new3ds_boot_progress_end(void);
bool new3ds_boot_progress_active(void);
bool new3ds_boot_progress_gfx_started(void);

#endif /* __3DS__ */
