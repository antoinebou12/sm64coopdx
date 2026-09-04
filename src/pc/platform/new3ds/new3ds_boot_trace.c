#ifdef __3DS__

#include "new3ds_boot_trace.h"

#include "new3ds_log.h"

#include <stdio.h>
#include <string.h>

#define NEW3DS_BOOT_LAST_PHASE_SIZE 96

static char sLastPhase[NEW3DS_BOOT_LAST_PHASE_SIZE] = "boot: not started";

void new3ds_boot_checkpoint(const char *phase) {
    if (phase == NULL || phase[0] == '\0') {
        phase = "(null)";
    }

    snprintf(sLastPhase, sizeof(sLastPhase), "%s", phase);
    sLastPhase[sizeof(sLastPhase) - 1] = '\0';

    new3ds_log_write("INFO", "boot", "checkpoint=%s", sLastPhase);
    new3ds_log_flush();
}

const char *new3ds_boot_trace_get_last(void) {
    return sLastPhase;
}

#endif /* __3DS__ */
