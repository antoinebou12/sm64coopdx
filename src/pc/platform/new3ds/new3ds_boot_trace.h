#pragma once

#ifdef __3DS__

void new3ds_boot_checkpoint(const char *phase);
const char *new3ds_boot_trace_get_last(void);

#endif /* __3DS__ */
