#include <stdio.h>
#include <string.h>

#include "coopnet_join_recovery.h"

void coopnet_join_recovery_reset(struct CoopNetJoinRecovery* recovery) {
    if (recovery == NULL) { return; }
    memset(recovery, 0, sizeof(*recovery));
    recovery->state = COOPNET_JOIN_RECOVERY_IDLE;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_join_result(
    struct CoopNetJoinRecovery* recovery,
    uint64_t lobbyId,
    const char* password,
    bool retry,
    bool accepted) {
    if (recovery == NULL || lobbyId == 0) {
        return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
    }

    if (retry) {
        if (recovery->state != COOPNET_JOIN_RECOVERY_RETRY_PENDING
            || recovery->lobbyId != lobbyId) {
            return COOPNET_JOIN_RECOVERY_ACTION_NONE;
        }
    } else {
        coopnet_join_recovery_reset(recovery);
        recovery->lobbyId = lobbyId;
        snprintf(recovery->password, sizeof(recovery->password), "%s", password != NULL ? password : "");
    }

    if (!accepted) {
        recovery->state = COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
        return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
    }

    recovery->state = COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN;
    return COOPNET_JOIN_RECOVERY_ACTION_NONE;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_timeout(struct CoopNetJoinRecovery* recovery) {
    if (recovery == NULL || recovery->state != COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }

    if (recovery->retryCount == 0) {
        recovery->retryCount = 1;
        recovery->state = COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION;
        return COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN;
    }

    recovery->state = COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
    return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_shutdown_complete(struct CoopNetJoinRecovery* recovery) {
    if (recovery == NULL || recovery->state != COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }

    recovery->state = COOPNET_JOIN_RECOVERY_WAITING_FOR_IDENTITY;
    return COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_reconnect_result(
    struct CoopNetJoinRecovery* recovery,
    bool accepted) {
    if (recovery == NULL || recovery->state != COOPNET_JOIN_RECOVERY_WAITING_FOR_IDENTITY) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }
    if (accepted) { return COOPNET_JOIN_RECOVERY_ACTION_NONE; }

    recovery->state = COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
    return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_connected(struct CoopNetJoinRecovery* recovery) {
    if (recovery == NULL || recovery->state != COOPNET_JOIN_RECOVERY_WAITING_FOR_IDENTITY) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }

    recovery->state = COOPNET_JOIN_RECOVERY_RETRY_PENDING;
    return COOPNET_JOIN_RECOVERY_ACTION_SEND_RETRY;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_disconnected(
    struct CoopNetJoinRecovery* recovery,
    bool intentional) {
    if (recovery == NULL || recovery->state == COOPNET_JOIN_RECOVERY_IDLE) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }
    if (intentional && recovery->state == COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }

    recovery->state = COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
    return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
}

enum CoopNetJoinRecoveryAction coopnet_join_recovery_fail(struct CoopNetJoinRecovery* recovery) {
    if (recovery == NULL || recovery->state == COOPNET_JOIN_RECOVERY_IDLE) {
        return COOPNET_JOIN_RECOVERY_ACTION_NONE;
    }
    recovery->state = COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
    return COOPNET_JOIN_RECOVERY_ACTION_FAIL;
}

bool coopnet_join_recovery_should_pump(const struct CoopNetJoinRecovery* recovery) {
    return recovery != NULL
        && recovery->state != COOPNET_JOIN_RECOVERY_IDLE
        && recovery->state != COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
}

bool coopnet_join_recovery_is_draining(const struct CoopNetJoinRecovery* recovery) {
    return recovery != NULL && recovery->state == COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION;
}

bool coopnet_join_recovery_failed(const struct CoopNetJoinRecovery* recovery) {
    return recovery != NULL && recovery->state == COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE;
}
