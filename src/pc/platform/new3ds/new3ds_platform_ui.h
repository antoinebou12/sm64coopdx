#pragma once

#ifdef __3DS__

/*
 * Minimal bottom-screen messages for boot failures on hardware that cannot run
 * the full game. Keeps aptMainLoop alive so HOME works while the user reads
 * the message and exits with START.
 */
void new3ds_platform_show_exit_message(const char *message);
const char *new3ds_platform_unsupported_hardware_message(void);
void new3ds_platform_quit(void) __attribute__((noreturn));

#endif /* __3DS__ */
