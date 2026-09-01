#ifdef __3DS__

#include <stdarg.h>
#include <stdio.h>

void log_to_terminal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void terminal_init(void) {
}

void terminal_update(void) {
}

void terminal_clear(void) {
}

#endif /* __3DS__ */
