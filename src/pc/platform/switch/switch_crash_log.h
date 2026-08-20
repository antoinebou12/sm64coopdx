#pragma once

#ifdef __SWITCH__

#ifdef __cplusplus
extern "C" {
#endif

const char *switch_crash_log_directory(void);
void switch_crash_log_checkpoint(const char *checkpoint);
void switch_crash_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */
