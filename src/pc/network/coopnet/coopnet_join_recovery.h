#ifndef COOPNET_JOIN_RECOVERY_H
#define COOPNET_JOIN_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#define COOPNET_JOIN_RECOVERY_PASSWORD_LENGTH 64

enum CoopNetJoinRecoveryState {
    COOPNET_JOIN_RECOVERY_IDLE,
    COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN,
    COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION,
    COOPNET_JOIN_RECOVERY_WAITING_FOR_IDENTITY,
    COOPNET_JOIN_RECOVERY_RETRY_PENDING,
    COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE,
};

enum CoopNetJoinRecoveryAction {
    COOPNET_JOIN_RECOVERY_ACTION_NONE,
    COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN,
    COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT,
    COOPNET_JOIN_RECOVERY_ACTION_SEND_RETRY,
    COOPNET_JOIN_RECOVERY_ACTION_FAIL,
};

struct CoopNetJoinRecovery {
    enum CoopNetJoinRecoveryState state;
    uint64_t lobbyId;
    char password[COOPNET_JOIN_RECOVERY_PASSWORD_LENGTH];
    unsigned int retryCount;
};

void coopnet_join_recovery_reset(struct CoopNetJoinRecovery* recovery);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_join_result(
    struct CoopNetJoinRecovery* recovery,
    uint64_t lobbyId,
    const char* password,
    bool retry,
    bool accepted);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_timeout(struct CoopNetJoinRecovery* recovery);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_shutdown_complete(struct CoopNetJoinRecovery* recovery);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_reconnect_result(
    struct CoopNetJoinRecovery* recovery,
    bool accepted);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_connected(struct CoopNetJoinRecovery* recovery);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_disconnected(
    struct CoopNetJoinRecovery* recovery,
    bool intentional);
enum CoopNetJoinRecoveryAction coopnet_join_recovery_fail(struct CoopNetJoinRecovery* recovery);
bool coopnet_join_recovery_should_pump(const struct CoopNetJoinRecovery* recovery);
bool coopnet_join_recovery_is_draining(const struct CoopNetJoinRecovery* recovery);
bool coopnet_join_recovery_failed(const struct CoopNetJoinRecovery* recovery);

#endif
