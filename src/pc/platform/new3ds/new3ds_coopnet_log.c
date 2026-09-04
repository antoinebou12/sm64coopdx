#if defined(__3DS__) && defined(COOPNET)

#include "new3ds_coopnet_log.h"
#include "new3ds_log.h"
#include "pc/configfile.h"

#include <3ds.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NEW3DS_COOPNET_ROOT "sdmc:/3ds/sm64coopdx"
#define NEW3DS_COOPNET_LOG_DIR NEW3DS_COOPNET_ROOT "/logs"
#define NEW3DS_COOPNET_LOG NEW3DS_COOPNET_LOG_DIR "/coopnet.log"
#define NEW3DS_COOPNET_CHECKPOINT NEW3DS_COOPNET_LOG_DIR "/coopnet_checkpoint.txt"
#define NEW3DS_COOPNET_COMMIT_INTERVAL 128u

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

static long long new3ds_coopnet_epoch(void) {
    return (long long)time(NULL);
}

static bool new3ds_coopnet_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool new3ds_coopnet_prepare(void) {
    if (sReady) {
        return true;
    }

    if (!new3ds_coopnet_mkdir("sdmc:/3ds")) {
        return false;
    }
    if (!new3ds_coopnet_mkdir(NEW3DS_COOPNET_ROOT)) {
        return false;
    }
    if (!new3ds_coopnet_mkdir(NEW3DS_COOPNET_LOG_DIR)) {
        return false;
    }

    sReady = true;
    return true;
}

void new3ds_coopnet_log_flush(bool force) {
    if (!sReady) {
        return;
    }
    if (!force && sPendingRecords < NEW3DS_COOPNET_COMMIT_INTERVAL) {
        return;
    }

    sPendingRecords = 0;
}

void new3ds_coopnet_log_printf(const char *fmt, ...) {
    if (!configNew3dsLogs || fmt == NULL || !new3ds_coopnet_prepare()) {
        return;
    }

    if (configNew3dsLogCoopnet) {
        va_list consoleArgs;
        va_start(consoleArgs, fmt);
        char consoleLine[NEW3DS_LOG_LINE_SIZE];
        vsnprintf(consoleLine, sizeof(consoleLine), fmt, consoleArgs);
        va_end(consoleArgs);
        NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_COOPNET, "coopnet", "%s", consoleLine);
    }

    FILE *file = fopen(NEW3DS_COOPNET_LOG, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "[%lld] seq=%" PRIu64 " ", new3ds_coopnet_epoch(), ++sSequence);

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    fputc('\n', file);
    fflush(file);
    fclose(file);

    ++sPendingRecords;
    new3ds_coopnet_log_flush(false);
}

void new3ds_coopnet_log_init(void) {
    if (!configNew3dsLogs || !new3ds_coopnet_prepare() || sSessionStarted) {
        return;
    }

    sSessionStarted = true;
    new3ds_coopnet_log_printf("=== CoopNet New 3DS session begin ===");
    new3ds_coopnet_log_flush(true);
}

void new3ds_coopnet_log_checkpoint(const char *component, const char *operation, const char *phase) {
    if (!configNew3dsLogs || !new3ds_coopnet_prepare()) {
        return;
    }

    if (component == NULL) { component = "unknown"; }
    if (operation == NULL) { operation = "unknown"; }
    if (phase == NULL) { phase = "unknown"; }

    new3ds_coopnet_log_printf("checkpoint component=%s operation=%s phase=%s",
                              component, operation, phase);

    FILE *file = fopen(NEW3DS_COOPNET_CHECKPOINT, "w");
    if (file != NULL) {
        fprintf(file, "epoch=%lld\n", new3ds_coopnet_epoch());
        fprintf(file, "sequence=%" PRIu64 "\n", sSequence);
        fprintf(file, "component=%s\n", component);
        fprintf(file, "operation=%s\n", operation);
        fprintf(file, "phase=%s\n", phase);
        fflush(file);
        fclose(file);
    }

    new3ds_coopnet_log_flush(true);
}

void new3ds_coopnet_log_tx(uint64_t bytes, int rc) {
    ++sTxPackets;
    sTxBytes += bytes;

    if (rc != 0) {
        ++sSendFailures;
        sLastSendRc = rc;
    }

    if (sTxPackets <= 5 || (sTxPackets % NEW3DS_COOPNET_COMMIT_INTERVAL) == 0 || rc != 0) {
        new3ds_coopnet_log_printf(
            "tx packets=%" PRIu64 " bytes=%" PRIu64 " last_bytes=%" PRIu64
            " rc=%d failures=%" PRIu64,
            sTxPackets, sTxBytes, bytes, rc, sSendFailures);
    }

    if (rc != 0) {
        new3ds_coopnet_log_flush(true);
    }
}

void new3ds_coopnet_log_rx(uint64_t bytes) {
    ++sRxPackets;
    sRxBytes += bytes;

    if (sRxPackets <= 5 || (sRxPackets % NEW3DS_COOPNET_COMMIT_INTERVAL) == 0) {
        new3ds_coopnet_log_printf(
            "rx packets=%" PRIu64 " bytes=%" PRIu64 " last_bytes=%" PRIu64,
            sRxPackets, sRxBytes, bytes);
    }
}

void new3ds_coopnet_log_shutdown_summary(void) {
    new3ds_coopnet_log_printf(
        "shutdown summary tx_packets=%" PRIu64 " tx_bytes=%" PRIu64
        " rx_packets=%" PRIu64 " rx_bytes=%" PRIu64
        " send_failures=%" PRIu64 " last_send_rc=%d",
        sTxPackets, sTxBytes, sRxPackets, sRxBytes, sSendFailures, sLastSendRc);
    new3ds_coopnet_log_printf("=== CoopNet New 3DS session end ===");
    new3ds_coopnet_log_flush(true);
}

void coopnet_horizon_diag(const char *event, uint64_t value0, uint64_t value1, const char *text) {
    new3ds_coopnet_log_init();

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
        new3ds_coopnet_log_printf("library event=%s value0=%" PRIu64 " value1=%" PRIu64 " text=%s",
                                  event, value0, value1, safeText);
    } else {
        new3ds_coopnet_log_printf("library event=%s value0=%" PRIu64 " value1=%" PRIu64,
                                  event, value0, value1);
    }

    const bool force = strstr(event, "error") != NULL
                    || strstr(event, "state") != NULL
                    || strstr(event, "selected") != NULL
                    || strstr(event, "connect_") != NULL
                    || strcmp(event, "ice.gathering_done") == 0;
    if (force) {
        new3ds_coopnet_log_flush(true);
    }
}

#endif /* __3DS__ && COOPNET */
