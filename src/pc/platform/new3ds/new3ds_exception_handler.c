#ifdef __3DS__

#include "new3ds_exception_handler.h"

#include "new3ds_boot_trace.h"
#include "new3ds_log.h"

#include <3ds.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NEW3DS_CRASH_LOG_ROOT "sdmc:/3ds/sm64coopdx"
#define NEW3DS_CRASH_LOG_DIR NEW3DS_CRASH_LOG_ROOT "/logs"
#define NEW3DS_CRASH_LOG NEW3DS_CRASH_LOG_DIR "/crash.log"
#define NEW3DS_EXCEPTION_STACK_SIZE (8 * 1024)

static u8 sExceptionStack[NEW3DS_EXCEPTION_STACK_SIZE] __attribute__((aligned(8)));
static ERRF_ExceptionData sExceptionData;

static bool new3ds_crash_log_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool new3ds_crash_log_prepare(void) {
    if (!new3ds_crash_log_mkdir("sdmc:/3ds")) {
        return false;
    }
    if (!new3ds_crash_log_mkdir(NEW3DS_CRASH_LOG_ROOT)) {
        return false;
    }
    if (!new3ds_crash_log_mkdir(NEW3DS_CRASH_LOG_DIR)) {
        return false;
    }
    return true;
}

static void new3ds_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs)
    __attribute__((noreturn, target("arm")));

static void new3ds_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs) {
    const char *last_checkpoint = new3ds_boot_trace_get_last();
    const u32 pc = (regs != NULL) ? regs->r[15] : 0;
    const u32 lr = (regs != NULL) ? regs->r[14] : 0;
    const u32 cpsr = (regs != NULL) ? regs->cpsr : 0;
    const u32 fault_type = (excep != NULL) ? (u32)excep->type : 0;
    const u32 fault_fsr = (excep != NULL) ? excep->fsr : 0;
    const u32 fault_far = (excep != NULL) ? excep->far : 0;

    new3ds_log_write(
        "ERROR",
        "crash",
        "fatal checkpoint=%s type=%lu fsr=0x%08lX far=0x%08lX pc=0x%08lX lr=0x%08lX cpsr=0x%08lX",
        last_checkpoint != NULL ? last_checkpoint : "(unknown)",
        (unsigned long)fault_type,
        (unsigned long)fault_fsr,
        (unsigned long)fault_far,
        (unsigned long)pc,
        (unsigned long)lr,
        (unsigned long)cpsr);
    new3ds_log_flush();

    if (new3ds_crash_log_prepare()) {
        FILE *file = fopen(NEW3DS_CRASH_LOG, "a");
        if (file != NULL) {
            fprintf(file, "=== SM64CoopDX crash epoch=%lld ===\n", (long long)time(NULL));
            fprintf(file, "checkpoint=%s\n", last_checkpoint != NULL ? last_checkpoint : "(unknown)");
            fprintf(file, "type=%lu\n", (unsigned long)fault_type);
            fprintf(file, "fsr=0x%08lX\n", (unsigned long)fault_fsr);
            fprintf(file, "far=0x%08lX\n", (unsigned long)fault_far);
            fprintf(file, "pc=0x%08lX\n", (unsigned long)pc);
            fprintf(file, "lr=0x%08lX\n", (unsigned long)lr);
            fprintf(file, "cpsr=0x%08lX\n", (unsigned long)cpsr);
            fclose(file);
        }
    }

    svcOutputDebugString("SM64CoopDX: fatal exception captured\n", 34);

    while (aptMainLoop()) {
        gspWaitForVBlank();
    }

    svcExitProcess();
    __builtin_unreachable();
}

void new3ds_exception_handler_install(void) {
#if NEW3DS_USER_EXCEPTIONS
    threadOnException(
        new3ds_exception_handler,
        sExceptionStack + NEW3DS_EXCEPTION_STACK_SIZE,
        &sExceptionData);
#else
    (void)sExceptionStack;
    (void)sExceptionData;
#endif
}

#endif /* __3DS__ */
