#pragma once

#ifdef __SWITCH__

#ifdef __cplusplus
extern "C" {
#endif

const char *switch_crash_log_directory(void);
void switch_crash_log_checkpoint(const char *checkpoint);
void switch_crash_log_printf(const char *fmt, ...);

/* Implemented in switch_startup_trace.c; logs an allocation-ladder + heap-region
 * memory snapshot tagged with `label`. Safe to call from any thread/point. */
void switch_startup_memory_probe(const char *label);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */
