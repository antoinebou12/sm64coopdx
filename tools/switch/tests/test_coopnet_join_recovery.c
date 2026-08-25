#include <stdio.h>
#include <string.h>

#include "pc/network/coopnet/coopnet_join_recovery.h"

static int sChecks;
static int sFailures;

#define CHECK(condition)                                                    \
    do {                                                                    \
        sChecks++;                                                          \
        if (!(condition)) {                                                 \
            sFailures++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

static void begin_join(struct CoopNetJoinRecovery* recovery) {
    CHECK(coopnet_join_recovery_join_result(recovery, 42, "secret", false, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(recovery->state == COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN);
    CHECK(recovery->lobbyId == 42);
    CHECK(strcmp(recovery->password, "secret") == 0);
}

static void test_clean_reconnect_and_retry(void) {
    struct CoopNetJoinRecovery recovery;
    coopnet_join_recovery_reset(&recovery);
    begin_join(&recovery);

    CHECK(coopnet_join_recovery_timeout(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN);
    CHECK(coopnet_join_recovery_is_draining(&recovery));
    CHECK(coopnet_join_recovery_should_pump(&recovery));
    CHECK(coopnet_join_recovery_disconnected(&recovery, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(coopnet_join_recovery_shutdown_complete(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT);
    CHECK(coopnet_join_recovery_reconnect_result(&recovery, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(coopnet_join_recovery_connected(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_SEND_RETRY);
    CHECK(recovery.state == COOPNET_JOIN_RECOVERY_RETRY_PENDING);
    CHECK(coopnet_join_recovery_join_result(&recovery, 42, "ignored", true, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(recovery.state == COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN);
    CHECK(recovery.retryCount == 1);
    CHECK(strcmp(recovery.password, "secret") == 0);
}

static void test_one_retry_limit(void) {
    struct CoopNetJoinRecovery recovery;
    coopnet_join_recovery_reset(&recovery);
    begin_join(&recovery);
    CHECK(coopnet_join_recovery_timeout(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN);
    CHECK(coopnet_join_recovery_shutdown_complete(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT);
    CHECK(coopnet_join_recovery_connected(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_SEND_RETRY);
    CHECK(coopnet_join_recovery_join_result(&recovery, 42, "secret", true, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(coopnet_join_recovery_timeout(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_FAIL);
    CHECK(coopnet_join_recovery_failed(&recovery));
    CHECK(!coopnet_join_recovery_should_pump(&recovery));
}

static void test_immediate_failures(void) {
    struct CoopNetJoinRecovery recovery;
    coopnet_join_recovery_reset(&recovery);
    CHECK(coopnet_join_recovery_join_result(&recovery, 42, "", false, false)
          == COOPNET_JOIN_RECOVERY_ACTION_FAIL);
    CHECK(coopnet_join_recovery_failed(&recovery));

    coopnet_join_recovery_reset(&recovery);
    begin_join(&recovery);
    CHECK(coopnet_join_recovery_timeout(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN);
    CHECK(coopnet_join_recovery_shutdown_complete(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT);
    CHECK(coopnet_join_recovery_reconnect_result(&recovery, false)
          == COOPNET_JOIN_RECOVERY_ACTION_FAIL);
    CHECK(coopnet_join_recovery_failed(&recovery));
}

static void test_cancel_and_late_callbacks(void) {
    struct CoopNetJoinRecovery recovery;
    coopnet_join_recovery_reset(&recovery);
    begin_join(&recovery);
    coopnet_join_recovery_reset(&recovery);
    CHECK(recovery.state == COOPNET_JOIN_RECOVERY_IDLE);
    CHECK(recovery.lobbyId == 0);
    CHECK(recovery.password[0] == '\0');
    CHECK(coopnet_join_recovery_connected(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(coopnet_join_recovery_disconnected(&recovery, true)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
    CHECK(coopnet_join_recovery_shutdown_complete(&recovery)
          == COOPNET_JOIN_RECOVERY_ACTION_NONE);
}

static void test_unexpected_disconnect_fails(void) {
    struct CoopNetJoinRecovery recovery;
    coopnet_join_recovery_reset(&recovery);
    begin_join(&recovery);
    CHECK(coopnet_join_recovery_disconnected(&recovery, false)
          == COOPNET_JOIN_RECOVERY_ACTION_FAIL);
    CHECK(coopnet_join_recovery_failed(&recovery));
}

int main(void) {
    printf("coopnet_join_recovery tests\n");
    test_clean_reconnect_and_retry();
    test_one_retry_limit();
    test_immediate_failures();
    test_cancel_and_late_callbacks();
    test_unexpected_disconnect_fails();

    if (sFailures != 0) {
        printf("%d/%d checks failed\n", sFailures, sChecks);
        return 1;
    }
    printf("all %d checks passed\n", sChecks);
    return 0;
}
