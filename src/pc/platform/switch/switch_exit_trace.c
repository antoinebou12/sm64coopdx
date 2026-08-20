#ifdef __SWITCH__

#include "switch_crash_log.h"

#include <switch.h>

#include <stdint.h>

/*
 * Every startup.log from the field shows rom_assets_load() completing and
 * every init stage after it completing, then userAppExit's "clean shutdown"
 * with no exception.log. That means the process is calling exit(0) on its
 * own, somewhere in the uninstrumented gap between "djui: late init complete"
 * and userAppExit - most likely via game_exit() (src/pc/pc_main.c), which can
 * be reached from four different places (an applet OnExitRequest hook, a
 * false return from appletMainLoop(), an SDL_QUIT event, or a failed
 * switch_platform_main_loop()). Wrapping both game_exit and the libc exit()
 * it calls captures whichever one actually fires, plus a short caller
 * backtrace, so the log tells us which route it was without needing to
 * reproduce it under a debugger.
 *
 * Because switch_platform_applet_hook() can be invoked directly by the
 * dedicated libnx applet-message thread (independent of anything the game
 * thread is doing), game_exit() can fire from a different thread than the
 * one blocked in rom_assets_load(). Record which thread called in, too.
 */

static volatile int sExitTraceDepth = 0;

static void switch_exit_trace_backtrace(void) {
    /*
     * Frame-pointer walk. Requires -fno-omit-frame-pointer on this
     * translation unit (set in Makefile.switch-game). u64 fp/lr pairs are
     * stored at [fp+0]/[fp+8] per the AArch64 procedure call standard.
     */
    uint64_t *fp = (uint64_t *)__builtin_frame_address(0);
    for (int i = 0; i < 8 && fp != NULL; ++i) {
        /* fp must be 16-byte aligned and point into a plausible stack range;
         * a bad frame chain must not fault the exception handler itself. */
        if (((uintptr_t)fp & 0xF) != 0) {
            break;
        }
        uint64_t next_fp = fp[0];
        uint64_t lr = fp[1];
        switch_crash_log_printf("  backtrace[%d]=0x%016llx", i, (unsigned long long)lr);
        if (next_fp == 0 || next_fp <= (uint64_t)(uintptr_t)fp) {
            break;
        }
        fp = (uint64_t *)(uintptr_t)next_fp;
    }
}

extern void __real_game_exit(void);
extern void __real_exit(int status) __attribute__((noreturn));

void __wrap_game_exit(void) {
    sExitTraceDepth++;
    if (sExitTraceDepth == 1) {
        switch_crash_log_printf(
            "game_exit called\ncaller=%p\nthread=0x%08x",
            __builtin_return_address(0),
            (unsigned int)threadGetCurHandle());
        switch_exit_trace_backtrace();
        switch_startup_memory_probe("game_exit");
    }
    __real_game_exit();
    sExitTraceDepth--;
}

void __wrap_exit(int status) {
    /*
     * game_exit() calls exit(0) itself, so this fires a second time for that
     * path; the depth guard keeps the log from getting a duplicate backtrace
     * while still logging every direct exit() call that did NOT go through
     * game_exit() (there should be none - if this fires with depth==1 and no
     * preceding "game_exit called" line, something is exiting some other
     * way).
     */
    sExitTraceDepth++;
    if (sExitTraceDepth == 1) {
        switch_crash_log_printf(
            "exit(%d) called directly\ncaller=%p\nthread=0x%08x",
            status,
            __builtin_return_address(0),
            (unsigned int)threadGetCurHandle());
        switch_exit_trace_backtrace();
    } else {
        switch_crash_log_printf("exit(%d) called via game_exit", status);
    }
    __real_exit(status);
}

#endif /* __SWITCH__ */
