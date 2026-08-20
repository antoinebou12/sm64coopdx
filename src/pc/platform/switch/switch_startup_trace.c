#ifdef __SWITCH__

#include "switch_crash_log.h"

/*
 * These functions are reached immediately after thread5_game_loop() during
 * Switch startup. The debug SD-layout build redirects them through GNU ld
 * --wrap so an instant crash leaves a durable last-known stage on the SD card.
 */
extern void __real_djui_init(void);
extern void __real_djui_unicode_init(void);
extern void __real_djui_init_late(void);

void __wrap_djui_init(void) {
    switch_crash_log_checkpoint("djui: init begin");
    __real_djui_init();
    switch_crash_log_checkpoint("djui: init complete");
}

void __wrap_djui_unicode_init(void) {
    switch_crash_log_checkpoint("djui: unicode init begin");
    __real_djui_unicode_init();
    switch_crash_log_checkpoint("djui: unicode init complete");
}

void __wrap_djui_init_late(void) {
    switch_crash_log_checkpoint("djui: late init begin");
    __real_djui_init_late();
    switch_crash_log_checkpoint("djui: late init complete");
}

#endif /* __SWITCH__ */
