#if defined(__SWITCH__) && defined(COOPNET)

#include "switch_coopnet_log.h"

#include <switch.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SWITCH_COOPNET_ROOT "sdmc:/switch/sm64coopdx"
#define SWITCH_COOPNET_LOG_DIR SWITCH_COOPNET_ROOT "/logs"
#define SWITCH_COOPNET_LOG SWITCH_COOPNET_LOG_DIR "/coopnet.log"
#define SWITCH_COOPNET_CHECKPOINT SWITCH_COOPNET_LOG_DIR "/coopnet_checkpoint.txt"
#define SWITCH_COOPNET_COMMIT_INTERVAL 128u

static bool sReady = false;
static bool sSessionStarted = false;
static uint64_t sSequence = 0;
static uint32_t sPendingRecords = 0;
static uint64_t sTxPackets = 0;
static uint64_t sTxBytes = 0;
static uint64_t sRxPackets = 0;
static uint64_t sRxBytes = 0;
static uint64_t sSendFailures = 0;
static int sLastSendRc = 0;

static long long switch_coopnet_epoch(void) {
    return (long long)time(NULL);
}

static bool switch_coopnet_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool switch_coopnet_prepare(void) {
    if (sReady) {
        return true;
    }

    if (!switch_coopnet_mkdir("sdmc:/switch")) {
        return false;
    }
    if (!switch_coopnet_mkdir(SWITCH_COOPNET_ROOT)) {
        return false;
    }
    if (!switch_coopnet_mkdir(SWITCH_COOPNET_LOG_DIR)) {
        return false;
    }

    sReady = true;
    return true;
}

void switch_coopnet_log_flush(bool force) {
    if (!sReady) {
        return;
    }
    if (!force && sPendingRecords < SWITCH_COOPNET_COMMIT_INTERVAL) {
        return;
    }

    (void)fsdevCommitDevice("sdmc");
    sPendingRecords = 0;
}

void switch_coopnet_log_printf(const char *fmt, ...) {
#ifdef SWITCH_NO_LOGS
    (void)fmt;
    return;
#endif
    if (fmt == NULL || !switch_coopnet_prepare()) {
        return;
    }

    FILE *file = fopen(SWITCH_COOPNET_LOG, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "[%lld] seq=%" PRIu64 " ", switch_coopnet_epoch(), ++sSequence);

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    fputc('\n', file);
    fflush(file);
    fclose(file);

    ++sPendingRecords;
    switch_coopnet_log_flush(false);
}

void switch_coopnet_log_init(void) {
#ifdef SWITCH_NO_LOGS
    return;
#endif
    if (!switch_coopnet_prepare() || sSessionStarted) {
        return;
    }

    sSessionStarted = true;
    switch_coopnet_log_printf("=== CoopNet Switch session begin === hos_version=0x%08" PRIx32,
                              hosversionGet());
    switch_coopnet_log_flush(true);
}

void switch_coopnet_log_checkpoint(const char *component, const char *operation, const char *phase) {
    if (!switch_coopnet_prepare()) {
        return;
    }

    if (component == NULL) { component = "unknown"; }
    if (operation == NULL) { operation = "unknown"; }
    if (phase == NULL) { phase = "unknown"; }

    switch_coopnet_log_printf("checkpoint component=%s operation=%s phase=%s",
                              component, operation, phase);

    FILE *file = fopen(SWITCH_COOPNET_CHECKPOINT, "w");
    if (file != NULL) {
        fprintf(file, "epoch=%lld\n", switch_coopnet_epoch());
        fprintf(file, "sequence=%" PRIu64 "\n", sSequence);
        fprintf(file, "component=%s\n", component);
        fprintf(file, "operation=%s\n", operation);
        fprintf(file, "phase=%s\n", phase);
        fflush(file);
        fclose(file);
    }

    switch_coopnet_log_flush(true);
}

void switch_coopnet_log_tx(uint64_t bytes, int rc) {
    ++sTxPackets;
    sTxBytes += bytes;

    if (rc != 0) {
        ++sSendFailures;
        sLastSendRc = rc;
    }

    if (sTxPackets <= 5 || (sTxPackets % SWITCH_COOPNET_COMMIT_INTERVAL) == 0 || rc != 0) {
        switch_coopnet_log_printf(
            "tx packets=%" PRIu64 " bytes=%" PRIu64 " last_bytes=%" PRIu64
            " rc=%d failures=%" PRIu64,
            sTxPackets, sTxBytes, bytes, rc, sSendFailures);
    }

    if (rc != 0) {
        switch_coopnet_log_flush(true);
    }
}

void switch_coopnet_log_rx(uint64_t bytes) {
    ++sRxPackets;
    sRxBytes += bytes;

    if (sRxPackets <= 5 || (sRxPackets % SWITCH_COOPNET_COMMIT_INTERVAL) == 0) {
        switch_coopnet_log_printf(
            "rx packets=%" PRIu64 " bytes=%" PRIu64 " last_bytes=%" PRIu64,
            sRxPackets, sRxBytes, bytes);
    }
}

void switch_coopnet_log_shutdown_summary(void) {
    switch_coopnet_log_printf(
        "shutdown summary tx_packets=%" PRIu64 " tx_bytes=%" PRIu64
        " rx_packets=%" PRIu64 " rx_bytes=%" PRIu64
        " send_failures=%" PRIu64 " last_send_rc=%d",
        sTxPackets, sTxBytes, sRxPackets, sRxBytes, sSendFailures, sLastSendRc);
    switch_coopnet_log_printf("=== CoopNet Switch session end ===");
    switch_coopnet_log_flush(true);
}

/*
 * Weak callback target used by the source-built CoopNet library's Horizon-only
 * diagnostics patch. The library remains usable without CoopDX; when this
 * symbol is present it can report TCP/DNS and libjuice ICE state without
 * linking directly against the game's logging implementation.
 *
 * Only non-secret fields are passed by the patch: server host/port, result
 * codes, peer IDs, ICE state strings, and ICE candidate SDP. Passwords, TURN
 * usernames/passwords, and auth tokens are never passed to this hook.
 */
void coopnet_horizon_diag(const char *event, uint64_t value0, uint64_t value1, const char *text) {
    switch_coopnet_log_init();

    if (event == NULL) {
        event = "unknown";
    }

    char safeText[512];
    safeText[0] = '\0';
    if (text != NULL) {
        size_t out = 0;
        for (size_t i = 0; text[i] != '\0' && out + 1 < sizeof(safeText); ++i) {
            const char c = text[i];
            safeText[out++] = (c == '\r' || c == '\n') ? ' ' : c;
        }
        safeText[out] = '\0';
    }

    if (safeText[0] != '\0') {
        switch_coopnet_log_printf("library event=%s value0=%" PRIu64 " value1=%" PRIu64 " text=%s",
                                  event, value0, value1, safeText);
    } else {
        switch_coopnet_log_printf("library event=%s value0=%" PRIu64 " value1=%" PRIu64,
                                  event, value0, value1);
    }

    const bool force = strstr(event, "error") != NULL
                    || strstr(event, "state") != NULL
                    || strstr(event, "selected") != NULL
                    || strstr(event, "connect_") != NULL
                    || strcmp(event, "ice.gathering_done") == 0;
    if (force) {
        switch_coopnet_log_flush(true);
    }
}

#endif /* __SWITCH__ && COOPNET */
