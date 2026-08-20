#pragma once

#ifdef __SWITCH__

#ifdef __cplusplus
extern "C" {
#endif

void switch_rom_asset_trace_printf(const char *fmt, ...);
unsigned int switch_rom_asset_trace_failure_count(void);
void switch_rom_asset_trace_exception_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */
