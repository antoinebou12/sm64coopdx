#pragma once

#ifdef __SWITCH__

#ifdef __cplusplus
extern "C" {
#endif

void switch_rom_asset_trace_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */
