#include <inttypes.h>
#include <time.h>
#include "libcoopnet.h"
#include "coopnet.h"
#include "coopnet_id.h"
#include "coopnet_join_recovery.h"
#include "pc/network/network.h"
#include "pc/network/version.h"
#include "pc/djui/djui_language.h"
#include "pc/djui/djui_popup.h"
#include "pc/djui/djui_panel_join_message.h"
#include "pc/mods/mods.h"
#include "pc/utils/misc.h"
#include "pc/debuglog.h"
#ifdef __SWITCH__
#include "pc/platform/switch/switch_coopnet_log.h"
#include "pc/platform/switch/switch_crash_log.h"
#endif
#if defined(__3DS__) && defined(COOPNET)
#include "pc/platform/new3ds/new3ds_coopnet_log.h"
#include "pc/platform/new3ds/new3ds_runtime.h"
#define switch_coopnet_log_init new3ds_coopnet_log_init
#define switch_coopnet_log_printf new3ds_coopnet_log_printf
#define switch_coopnet_log_checkpoint new3ds_coopnet_log_checkpoint
#define switch_coopnet_log_flush new3ds_coopnet_log_flush
#define switch_coopnet_log_tx new3ds_coopnet_log_tx
#define switch_coopnet_log_rx new3ds_coopnet_log_rx
#define switch_coopnet_log_shutdown_summary new3ds_coopnet_log_shutdown_summary
#define switch_crash_log_checkpoint(phase) new3ds_coopnet_log_checkpoint("crash", phase, "checkpoint")
#define switch_crash_log_printf new3ds_coopnet_log_printf
#endif
#ifdef DISCORD_SDK
#include "pc/discord/discord.h"
#endif

#ifdef COOPNET

#define MAX_COOPNET_DESCRIPTION_LENGTH 1024

#if defined(__SWITCH__) || defined(__3DS__)
#define COOPNET_GAME_NAME "sm64coop-android"
#else
#define COOPNET_GAME_NAME GAME_NAME
#endif

uint64_t gCoopNetDesiredLobby = 0;
char gCoopNetPassword[64] = "";
char sCoopNetDescription[MAX_COOPNET_DESCRIPTION_LENGTH] = "";

static uint64_t sLocalLobbyId = 0;
static uint64_t sLocalLobbyOwnerId = 0;
static enum NetworkType sNetworkType;
static bool sReconnecting = false;
static QueryCallbackPtr sQueryCallback = NULL;
static QueryFinishCallbackPtr sQueryFinishCallback = NULL;
#if defined(__SWITCH__) || defined(__3DS__)
static bool sPendingModListRequest = false;
static uint64_t sConnectedPeerIds[MAX_PLAYERS] = { 0 };
static time_t sCoopNetJoinStartTime = 0;
static struct CoopNetJoinRecovery sCoopNetJoinRecovery = { 0 };
static const char* sCoopNetJoinFailureReason = NULL;
#endif

static CoopNetRc coopnet_initialize(void);

#if defined(__SWITCH__) || defined(__3DS__)
static void coopnet_switch_clear_connected_peers(void) {
    memset(sConnectedPeerIds, 0, sizeof(sConnectedPeerIds));
}

static const char* coopnet_switch_join_state_name(enum CoopNetJoinRecoveryState state) {
    switch (state) {
        case COOPNET_JOIN_RECOVERY_IDLE:                return "idle";
        case COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN:    return "waiting_for_join";
        case COOPNET_JOIN_RECOVERY_DRAINING_CONNECTION: return "draining_connection";
        case COOPNET_JOIN_RECOVERY_WAITING_FOR_IDENTITY: return "waiting_for_identity";
        case COOPNET_JOIN_RECOVERY_RETRY_PENDING:       return "retry_pending";
        case COOPNET_JOIN_RECOVERY_TERMINAL_FAILURE:    return "terminal_failure";
    }
    return "unknown";
}

static void coopnet_switch_reset_join_recovery(const char* reason) {
    if (sCoopNetJoinRecovery.state != COOPNET_JOIN_RECOVERY_IDLE) {
        switch_coopnet_log_printf("join recovery reset state=%s reason=%s",
                                  coopnet_switch_join_state_name(sCoopNetJoinRecovery.state),
                                  reason != NULL ? reason : "unspecified");
    }
    coopnet_join_recovery_reset(&sCoopNetJoinRecovery);
    sCoopNetJoinStartTime = 0;
    sCoopNetJoinFailureReason = NULL;
}

static void coopnet_switch_mark_join_failure(const char* reason) {
    sCoopNetJoinFailureReason = reason;
    coopnet_join_recovery_fail(&sCoopNetJoinRecovery);
}

static void coopnet_switch_start_join_reconnect(void) {
    enum CoopNetJoinRecoveryAction action =
        coopnet_join_recovery_shutdown_complete(&sCoopNetJoinRecovery);
    if (action != COOPNET_JOIN_RECOVERY_ACTION_START_RECONNECT) { return; }

    switch_coopnet_log_printf("join recovery shutdown complete; starting reconnect lobby_id=%" PRIu64,
                              sCoopNetJoinRecovery.lobbyId);
    CoopNetRc initRc = coopnet_initialize();
    switch_coopnet_log_printf("join recovery reconnect begin rc=%d", (int)initRc);
    if (coopnet_join_recovery_reconnect_result(&sCoopNetJoinRecovery, initRc == COOPNET_OK)
        == COOPNET_JOIN_RECOVERY_ACTION_FAIL) {
        sCoopNetJoinFailureReason = "could not reconnect to CoopNet";
    }
    switch_coopnet_log_flush(true);
}

static void coopnet_switch_return_from_public_join(const char* message, const char* reason) {
    switch_coopnet_log_printf("public join terminated lobby_id=%" PRIu64 " reason=%s",
                              sCoopNetJoinRecovery.lobbyId != 0
                                  ? sCoopNetJoinRecovery.lobbyId
                                  : gCoopNetDesiredLobby,
                              reason != NULL ? reason : "unspecified");
    switch_coopnet_log_flush(true);

    sPendingModListRequest = false;
    sLocalLobbyId = 0;
    sLocalLobbyOwnerId = 0;
    sNetworkType = NT_NONE;
    coopnet_switch_clear_connected_peers();
    coopnet_switch_reset_join_recovery(reason);

    /* Leave gameplay mode but retain the healthy signaling client so the
     * parent public-lobby list can issue a fresh query immediately. */
    network_shutdown(false, false, false, true);
    djui_panel_join_message_return_to_lobbies(message);
}

static void coopnet_switch_finish_join_failure(void) {
    const char* reason = sCoopNetJoinFailureReason != NULL
        ? sCoopNetJoinFailureReason
        : "lobby did not respond after retry";
    switch_coopnet_log_printf("join recovery terminal failure lobby_id=%" PRIu64 " retry=%u reason=%s",
                              sCoopNetJoinRecovery.lobbyId,
                              sCoopNetJoinRecovery.retryCount,
                              reason);
    switch_crash_log_checkpoint("network: lobby join failed");
    switch_coopnet_log_flush(true);

    sPendingModListRequest = false;
    sLocalLobbyId = 0;
    sLocalLobbyOwnerId = 0;
    sNetworkType = NT_NONE;
    coopnet_switch_clear_connected_peers();
    coopnet_switch_reset_join_recovery("terminal failure handled");

    /* Clean up gameplay state while retaining a healthy signaling connection
     * for the lobby-list query that follows. */
    network_shutdown(false, false, false, true);
    djui_panel_join_message_return_to_lobbies(
        "Lobby did not respond; it may be stale or unreachable.");
}

static bool coopnet_switch_peer_is_connected(uint64_t peerId) {
    if (peerId == 0) { return false; }
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        if (sConnectedPeerIds[i] == peerId) {
            return true;
        }
    }
    return false;
}

static void coopnet_switch_set_peer_connected(uint64_t peerId, bool connected) {
    if (peerId == 0) { return; }

    if (!connected) {
        for (u32 i = 0; i < MAX_PLAYERS; i++) {
            if (sConnectedPeerIds[i] == peerId) {
                sConnectedPeerIds[i] = 0;
            }
        }
        return;
    }

    if (coopnet_switch_peer_is_connected(peerId)) {
        return;
    }

    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        if (sConnectedPeerIds[i] == 0) {
            sConnectedPeerIds[i] = peerId;
            return;
        }
    }

    switch_coopnet_log_printf("peer tracking table full peer_id=%" PRIu64, peerId);
}

static void coopnet_switch_try_send_mod_list_request(const char* trigger) {
    if (!sPendingModListRequest || gNetworkType != NT_CLIENT || sLocalLobbyOwnerId == 0) {
        return;
    }

    if (!coopnet_switch_peer_is_connected(sLocalLobbyOwnerId)) {
        switch_coopnet_log_printf("mod list request still deferred trigger=%s owner_id=%" PRIu64,
                                  trigger != NULL ? trigger : "unknown", sLocalLobbyOwnerId);
        return;
    }

    sPendingModListRequest = false;
    switch_coopnet_log_printf("peer ready, sending deferred mod list request trigger=%s owner_id=%" PRIu64,
                              trigger != NULL ? trigger : "unknown", sLocalLobbyOwnerId);
    switch_crash_log_checkpoint("network: mod list request sent");
    network_send_mod_list_request();
}
#endif

static void coopnet_on_lobby_list_got(uint64_t lobbyId, uint64_t ownerId, uint16_t connections,
                                      uint16_t maxConnections, const char* game, const char* version,
                                      const char* hostName, const char* mode, const char* description) {
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby list item lobby_id=%" PRIu64 " owner_id=%" PRIu64
                              " connections=%u max_connections=%u",
                              lobbyId, ownerId, connections, maxConnections);
#endif
    if (sQueryCallback != NULL) {
        sQueryCallback(lobbyId, ownerId, connections, maxConnections, game, version, hostName, mode, description);
    }
}

static void coopnet_on_lobby_list_finish(void) {
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby list finish");
    switch_coopnet_log_flush(true);
#endif
    if (sQueryFinishCallback != NULL) {
        sQueryFinishCallback();
    }
}

bool ns_coopnet_query(QueryCallbackPtr callback, QueryFinishCallbackPtr finishCallback, const char* password) {
    sQueryCallback = callback;
    sQueryFinishCallback = finishCallback;

    if (coopnet_initialize() != COOPNET_OK) {
#if defined(__SWITCH__) || defined(__3DS__)
        switch_coopnet_log_printf("lobby list initialize failed");
        switch_coopnet_log_flush(true);
#endif
        return false;
    }

    gCoopNetCallbacks.OnLobbyListGot = coopnet_on_lobby_list_got;
    gCoopNetCallbacks.OnLobbyListFinish = coopnet_on_lobby_list_finish;

#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_list_get", "BEFORE");
#endif
    CoopNetRc rc = coopnet_lobby_list_get(COOPNET_GAME_NAME, password);
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby list request game=%s rc=%d", COOPNET_GAME_NAME, (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_list_get", "AFTER");
#endif
    return rc == COOPNET_OK;
}

static void coopnet_on_connected(uint64_t userId) {
#if defined(__SWITCH__) || defined(__3DS__)
    coopnet_switch_clear_connected_peers();
    switch_coopnet_log_printf("signaling connected user_id=%" PRIu64, userId);
#endif
    coopnet_set_local_user_id(userId);
#if defined(__SWITCH__) || defined(__3DS__)
    enum CoopNetJoinRecoveryAction action = coopnet_join_recovery_connected(&sCoopNetJoinRecovery);
    if (action == COOPNET_JOIN_RECOVERY_ACTION_SEND_RETRY) {
        gCoopNetDesiredLobby = sCoopNetJoinRecovery.lobbyId;
        snprintf(gCoopNetPassword, sizeof(gCoopNetPassword), "%s", sCoopNetJoinRecovery.password);
        sNetworkType = NT_CLIENT;
        sReconnecting = false;
        switch_coopnet_log_printf("join recovery identity received; retry pending lobby_id=%" PRIu64,
                                  sCoopNetJoinRecovery.lobbyId);
    }
    switch_coopnet_log_flush(true);
#endif
}

static void coopnet_on_disconnected(bool intentional) {
    LOG_INFO("Coopnet shutdown!");
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("signaling disconnected intentional=%d", intentional ? 1 : 0);
    switch_coopnet_log_flush(true);
    const bool recovering = coopnet_join_recovery_should_pump(&sCoopNetJoinRecovery);
    enum CoopNetJoinRecoveryAction recoveryAction =
        coopnet_join_recovery_disconnected(&sCoopNetJoinRecovery, intentional);
    if (recovering) {
        sPendingModListRequest = false;
        coopnet_switch_clear_connected_peers();
        if (recoveryAction == COOPNET_JOIN_RECOVERY_ACTION_FAIL) {
            sCoopNetJoinFailureReason = "signaling disconnected during join recovery";
            switch_coopnet_log_printf("join recovery disconnect became terminal state=%s",
                                      coopnet_switch_join_state_name(sCoopNetJoinRecovery.state));
            CoopNetRc shutdownRc = coopnet_shutdown();
            switch_coopnet_log_printf("join recovery disconnected-client shutdown rc=%d", (int)shutdownRc);
        } else {
            switch_coopnet_log_printf("join recovery observed expected shutdown disconnect");
        }
        switch_coopnet_log_flush(true);
        return;
    }
#endif
    if (!intentional) {
        djui_popup_create(DLANG(NOTIF, COOPNET_DISCONNECTED), 2);
    }
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "BEFORE");
#endif
    CoopNetRc rc = coopnet_shutdown();
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("disconnect callback shutdown rc=%d", (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "AFTER");
    sPendingModListRequest = false;
    coopnet_switch_clear_connected_peers();
#endif
    gCoopNetCallbacks.OnLobbyListGot = NULL;
    gCoopNetCallbacks.OnLobbyListFinish = NULL;
}

static void coopnet_on_lobby_created(uint64_t lobbyId, const char* game, const char* version,
                                     const char* hostName, const char* mode, uint16_t maxConnections) {
    (void)game;
    (void)version;
    (void)hostName;
    (void)mode;
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby created lobby_id=%" PRIu64 " max_connections=%u",
                              lobbyId, maxConnections);
    switch_coopnet_log_flush(true);
#endif
}

static void coopnet_on_peer_connected(uint64_t peerId) {
#if defined(__SWITCH__) || defined(__3DS__)
    coopnet_switch_set_peer_connected(peerId, true);
    switch_coopnet_log_printf("peer connected peer_id=%" PRIu64, peerId);
    coopnet_switch_try_send_mod_list_request("peer_connected");
    switch_coopnet_log_flush(true);
#else
    (void)peerId;
#endif
}

static void coopnet_on_peer_disconnected(uint64_t peerId) {
#if defined(__SWITCH__) || defined(__3DS__)
    coopnet_switch_set_peer_connected(peerId, false);
    switch_coopnet_log_printf("peer disconnected peer_id=%" PRIu64, peerId);
    if (peerId == sLocalLobbyOwnerId && gNetworkType == NT_CLIENT) {
        sPendingModListRequest = true;
    }
    switch_coopnet_log_flush(true);
#endif
    u8 localIndex = coopnet_user_id_to_local_index(peerId);
    if (localIndex != UNKNOWN_LOCAL_INDEX && gNetworkPlayers[localIndex].connected) {
        network_player_disconnected(gNetworkPlayers[localIndex].globalIndex);
    }
}

static void coopnet_on_load_balance(const char* host, uint32_t port) {
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("signaling load-balance host=%s port=%u",
                              host != NULL ? host : "(null)", port);
    switch_coopnet_log_flush(true);
#endif
    if (host && strlen(host) > 0) {
        snprintf(configCoopNetIp, MAX_CONFIG_STRING, "%s", host);
    }
    configCoopNetPort = port;
    configfile_save(configfile_name());
}

static void coopnet_on_receive(uint64_t userId, const uint8_t* data, uint64_t dataLength) {
#if defined(__SWITCH__) || defined(__3DS__)
    static uint64_t sRxCount = 0;
    ++sRxCount;
    switch_coopnet_log_rx(dataLength);
    if (dataLength > 0 && (sRxCount <= 10 || (sRxCount % 256) == 0)) {
        switch_coopnet_log_printf("coopnet receive user_id=%" PRIu64 " len=%" PRIu64 " first_byte=0x%02x rx_count=%" PRIu64,
                                  userId, dataLength, data[0], sRxCount);
        switch_coopnet_log_flush(true);
    }
#endif
    coopnet_set_user_id(0, userId);
    u8 localIndex = coopnet_user_id_to_local_index(userId);
    network_receive(localIndex, &userId, (u8*)data, dataLength);
}

static void coopnet_on_lobby_joined(uint64_t lobbyId, uint64_t userId, uint64_t ownerId, uint64_t destId) {
    LOG_INFO("coopnet_on_lobby_joined!");
    const bool localJoin = (userId == coopnet_get_local_user_id());
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby joined event lobby_id=%" PRIu64 " user_id=%" PRIu64
                              " owner_id=%" PRIu64 " local=%d",
                              lobbyId, userId, ownerId, localJoin ? 1 : 0);
    if (localJoin) {
        coopnet_switch_reset_join_recovery("local lobby joined");
        switch_crash_log_checkpoint("network: local lobby joined");
    } else if (sCoopNetJoinRecovery.lobbyId != 0) {
        switch_coopnet_log_printf("ignoring remote lobby-joined event while waiting for local join lobby_id=%" PRIu64,
                                  sCoopNetJoinRecovery.lobbyId);
    }
    switch_coopnet_log_flush(true);
#endif

    coopnet_set_user_id(0, ownerId);

    if (localJoin) {
        sLocalLobbyId = lobbyId;
        sLocalLobbyOwnerId = ownerId;
        coopnet_clear_dest_ids();
        snprintf(configDestId, MAX_CONFIG_STRING, "%" PRIu64 "", destId);
    }

    coopnet_save_dest_id(userId, destId);

    if (localJoin && gNetworkType == NT_CLIENT) {
#if defined(__SWITCH__) || defined(__3DS__)
        /* libcoopnet can report OnPeerConnected either before or after lobby
         * membership. Track both states so whichever callback arrives second
         * sends the request exactly once. */
        sPendingModListRequest = true;
        switch_coopnet_log_printf("mod list request deferred until ICE peer connection owner_id=%" PRIu64,
                                  ownerId);
        coopnet_switch_try_send_mod_list_request("lobby_joined");
        switch_coopnet_log_flush(true);
#else
        network_send_mod_list_request();
#endif
    }
#ifdef DISCORD_SDK
    if (gDiscordInitialized) {
        discord_activity_update();
    }
#endif
}

static void coopnet_on_lobby_left(uint64_t lobbyId, uint64_t userId) {
    LOG_INFO("coopnet_on_lobby_left!");
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("lobby left lobby_id=%" PRIu64 " user_id=%" PRIu64,
                              lobbyId, userId);
    if (userId == coopnet_get_local_user_id()) {
        coopnet_switch_reset_join_recovery("local lobby left");
        sPendingModListRequest = false;
        coopnet_switch_clear_connected_peers();
    }
    switch_coopnet_log_flush(true);
#endif
    coopnet_clear_dest_id(userId);
    if (lobbyId == sLocalLobbyId && userId == coopnet_get_local_user_id()) {
        network_shutdown(false, false, true, false);
    }
}

static void coopnet_on_error(enum MPacketErrorNumber error, uint64_t tag) {
#if defined(__SWITCH__) || defined(__3DS__)
    const bool rejectedPublicJoin = error == MERR_LOBBY_JOIN_FAILED
        && sCoopNetJoinRecovery.state != COOPNET_JOIN_RECOVERY_IDLE
        && sCoopNetJoinRecovery.password[0] == '\0';
    switch_coopnet_log_printf("coopnet error=%d tag=%" PRIu64, (int)error, tag);
    switch_coopnet_log_flush(true);
    if (!rejectedPublicJoin) {
        coopnet_switch_reset_join_recovery("coopnet error callback");
    }
#endif
    switch (error) {
        case MERR_COOPNET_VERSION:
            djui_popup_create(DLANG(NOTIF, COOPNET_VERSION), 2);
            network_shutdown(false, false, false, false);
            break;
        case MERR_PEER_FAILED:
            {
                char built[256] = { 0 };
                u8 localIndex = coopnet_user_id_to_local_index(tag);
                char* name = DLANG(NOTIF, UNKNOWN);
                if (localIndex == 0) {
                    name = DLANG(NOTIF, LOBBY_HOST);
                } else if (localIndex != UNKNOWN_LOCAL_INDEX && gNetworkPlayers[localIndex].connected) {
                    name = gNetworkPlayers[localIndex].name;
                }
                djui_language_replace(DLANG(NOTIF, PEER_FAILED), built, 256, '@', name);
                djui_popup_create(built, 2);
            }
            break;
        case MERR_LOBBY_NOT_FOUND:
            djui_popup_create(DLANG(NOTIF, LOBBY_NOT_FOUND), 2);
            network_shutdown(false, false, false, false);
            break;
        case MERR_LOBBY_JOIN_FULL:
            djui_popup_create(DLANG(NOTIF, DISCONNECT_FULL), 2);
            network_shutdown(false, false, false, false);
            break;
        case MERR_LOBBY_JOIN_FAILED:
#if defined(__SWITCH__) || defined(__3DS__)
            if (rejectedPublicJoin) {
                switch_crash_log_checkpoint("network: public lobby admission denied");
                coopnet_switch_return_from_public_join(
                    "This Switch build is not authorized for public CoopNet lobbies.",
                    "server denied public lobby admission");
                break;
            }
#endif
            djui_popup_create(DLANG(NOTIF, LOBBY_JOIN_FAILED), 2);
            network_shutdown(false, false, false, false);
            break;
        case MERR_LOBBY_PASSWORD_INCORRECT:
            djui_popup_create(DLANG(NOTIF, LOBBY_PASSWORD_INCORRECT), 2);
            network_shutdown(false, false, false, false);
            break;
        case MERR_NONE:
        case MERR_MAX:
            break;
    }
}

static bool ns_coopnet_initialize(enum NetworkType networkType, bool reconnecting) {
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_init();
    switch_coopnet_log_printf("network initialize type=%d reconnecting=%d",
                              (int)networkType, reconnecting ? 1 : 0);
    if (!reconnecting && networkType == NT_CLIENT) {
        coopnet_switch_reset_join_recovery("new client initialization");
        sPendingModListRequest = false;
        coopnet_switch_clear_connected_peers();
    } else if (!reconnecting && networkType != NT_CLIENT) {
        coopnet_switch_reset_join_recovery("non-client initialization");
        sPendingModListRequest = false;
        coopnet_switch_clear_connected_peers();
    }
#endif
    sNetworkType = networkType;
    sReconnecting = reconnecting;
    if (reconnecting) { return true; }
    return coopnet_is_connected()
        ? true
        : (coopnet_initialize() == COOPNET_OK);
}

static char* ns_coopnet_get_id_str(u8 localIndex) {
    static char id_str[32] = { 0 };
    if (localIndex == UNKNOWN_LOCAL_INDEX) {
        snprintf(id_str, 32, "???");
    } else {
        uint64_t userId = ns_coopnet_get_id(localIndex);
        uint64_t destId = coopnet_get_dest_id(userId);
        snprintf(id_str, 32, "%" PRIu64 "", destId);
    }
    return id_str;
}

static bool ns_coopnet_match_addr(void* addr1, void* addr2) {
    return !memcmp(addr1, addr2, sizeof(u64));
}

bool ns_coopnet_is_connected(void) {
    return coopnet_is_connected();
}

static void coopnet_populate_description(void) {
    char* buffer = sCoopNetDescription;
    int bufferLength = MAX_COOPNET_DESCRIPTION_LENGTH;
    const char* version = get_version();
    int versionLength = strlen(version);
    snprintf(buffer, bufferLength, "%s", version);
    buffer += versionLength;
    bufferLength -= versionLength;

    if (gActiveMods.entryCount <= 0) { return; }
    char* strings[gActiveMods.entryCount];
    for (int i = 0; i < gActiveMods.entryCount; i++) {
        struct Mod* mod = gActiveMods.entries[i];
        strings[i] = mod->name;
    }

    char* sep = "\n\nMods:\n";
    snprintf(buffer, bufferLength, "%s", sep);
    buffer += strlen(sep);
    bufferLength -= strlen(sep);

    str_seperator_concat(buffer, bufferLength, strings, gActiveMods.entryCount, "\\#\\\n");
}

void ns_coopnet_update(void) {
#if defined(__SWITCH__) || defined(__3DS__)
    if (!coopnet_is_connected() && !coopnet_join_recovery_should_pump(&sCoopNetJoinRecovery)) { return; }
#else
    if (!coopnet_is_connected()) { return; }
#endif

    CoopNetRc updateRc = coopnet_update();
#if defined(__SWITCH__) || defined(__3DS__)
    if (updateRc != COOPNET_OK) {
        switch_coopnet_log_printf("coopnet_update rc=%d", (int)updateRc);
        switch_coopnet_log_flush(true);
    }

    if (coopnet_join_recovery_is_draining(&sCoopNetJoinRecovery)
        && updateRc == COOPNET_DISCONNECTED) {
        coopnet_switch_start_join_reconnect();
    }

    if (coopnet_join_recovery_failed(&sCoopNetJoinRecovery)) {
        coopnet_switch_finish_join_failure();
        return;
    }

    if (sCoopNetJoinRecovery.state == COOPNET_JOIN_RECOVERY_WAITING_FOR_JOIN
        && sCoopNetJoinStartTime != 0) {
        time_t now = time(NULL);
        if (now - sCoopNetJoinStartTime >= 15) {
            switch_coopnet_log_printf("coopnet local lobby join timeout lobby_id=%" PRIu64 " elapsed=%ld retry=%d",
                                      sCoopNetJoinRecovery.lobbyId,
                                      (long)(now - sCoopNetJoinStartTime),
                                      (int)sCoopNetJoinRecovery.retryCount);
            switch_coopnet_log_printf("coopnet join state local_user_id=%" PRIu64 " local_lobby_id=%" PRIu64 " owner_id=%" PRIu64,
                                      coopnet_get_local_user_id(), sLocalLobbyId, sLocalLobbyOwnerId);
            switch_coopnet_log_flush(true);

            sCoopNetJoinStartTime = 0;
            sPendingModListRequest = false;
            sLocalLobbyId = 0;
            sLocalLobbyOwnerId = 0;
            coopnet_switch_clear_connected_peers();

            enum CoopNetJoinRecoveryAction action =
                coopnet_join_recovery_timeout(&sCoopNetJoinRecovery);
            if (action == COOPNET_JOIN_RECOVERY_ACTION_FAIL) {
                sCoopNetJoinFailureReason = "lobby did not respond after fresh-connection retry";
                switch_crash_log_checkpoint("network: lobby join timed out");
            } else if (action == COOPNET_JOIN_RECOVERY_ACTION_REQUEST_SHUTDOWN) {
                switch_coopnet_log_printf("coopnet join recovery: fresh connection retry lobby=%" PRIu64,
                                          sCoopNetJoinRecovery.lobbyId);
                switch_crash_log_checkpoint("network: lobby join retry");
                CoopNetRc shutdownRc = coopnet_shutdown();
                switch_coopnet_log_printf("coopnet join recovery shutdown rc=%d", (int)shutdownRc);
                switch_coopnet_log_flush(true);
                if (shutdownRc == COOPNET_DISCONNECTED) {
                    coopnet_switch_start_join_reconnect();
                } else if (shutdownRc != COOPNET_OK) {
                    coopnet_switch_mark_join_failure("could not shut down stale CoopNet connection");
                }
            }
        }
    }

    if (coopnet_join_recovery_failed(&sCoopNetJoinRecovery)) {
        coopnet_switch_finish_join_failure();
        return;
    }

    if (!coopnet_is_connected()) { return; }
#endif
    if (gNetworkType != NT_NONE && sNetworkType != NT_NONE) {
        if (sNetworkType == NT_SERVER) {
            char mode[64] = "";
            mods_get_main_mod_name(mode, 64);
            if (sReconnecting) {
                LOG_INFO("Update lobby");
                coopnet_populate_description();
#if defined(__SWITCH__) || defined(__3DS__)
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_update", "BEFORE");
#endif
                CoopNetRc rc = coopnet_lobby_update(sLocalLobbyId, COOPNET_GAME_NAME, get_version(), configPlayerName, mode, sCoopNetDescription);
#if defined(__SWITCH__) || defined(__3DS__)
                switch_coopnet_log_printf("lobby update lobby_id=%" PRIu64 " rc=%d", sLocalLobbyId, (int)rc);
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_update", "AFTER");
#endif
            } else {
                LOG_INFO("Create lobby");
                snprintf(gCoopNetPassword, 64, "%s", configPassword);
                coopnet_populate_description();
#if defined(__SWITCH__) || defined(__3DS__)
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_create", "BEFORE");
#endif
                CoopNetRc rc = coopnet_lobby_create(COOPNET_GAME_NAME, get_version(), configPlayerName, mode,
                                                     (uint16_t)configAmountOfPlayers, gCoopNetPassword,
                                                     sCoopNetDescription);
#if defined(__SWITCH__) || defined(__3DS__)
                switch_coopnet_log_printf("lobby create max_players=%u rc=%d",
                                          (unsigned)configAmountOfPlayers, (int)rc);
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_create", "AFTER");
#endif
            }
        } else if (sNetworkType == NT_CLIENT) {
            LOG_INFO("Join lobby");
#if defined(__SWITCH__) || defined(__3DS__)
            const bool retry = sCoopNetJoinRecovery.state == COOPNET_JOIN_RECOVERY_RETRY_PENDING;
            const bool publicJoin = gCoopNetPassword[0] == '\0';
            if (publicJoin) {
                const uint64_t clientHash = coopnet_get_client_hash();
                switch_coopnet_log_printf("public join identity preflight lobby_id=%" PRIu64
                                          " fingerprint=%" PRIu64 " hex=0x%016" PRIx64,
                                          gCoopNetDesiredLobby, clientHash, clientHash);
                if (clientHash == 0) {
                    switch_crash_log_checkpoint("network: public join identity unavailable");
#if defined(__3DS__)
                    coopnet_switch_return_from_public_join(
                        "3DS build identity unavailable; reinstall sm64coopdx.3dsx at sdmc:/3ds/sm64coopdx/.",
                        "public join blocked because client fingerprint is zero");
#else
                    coopnet_switch_return_from_public_join(
                        "Switch build identity unavailable; reinstall sm64coopdx.nro at /switch/sm64coopdx/.",
                        "public join blocked because client fingerprint is zero");
#endif
                    return;
                }
            }
#endif
#if defined(__SWITCH__) || defined(__3DS__)
            switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_join", "BEFORE");
            switch_coopnet_log_printf("coopnet join attempt lobby_id=%" PRIu64 " local_user_id=%" PRIu64 " signaling_connected=%d retry=%d",
                                      gCoopNetDesiredLobby, coopnet_get_local_user_id(), coopnet_is_connected() ? 1 : 0,
                                      retry ? 1 : 0);
            switch_crash_log_checkpoint("network: lobby join attempted");
#endif
            CoopNetRc rc = coopnet_lobby_join(gCoopNetDesiredLobby, gCoopNetPassword);
#if defined(__SWITCH__) || defined(__3DS__)
            enum CoopNetJoinRecoveryAction joinAction = coopnet_join_recovery_join_result(
                &sCoopNetJoinRecovery,
                gCoopNetDesiredLobby,
                gCoopNetPassword,
                retry,
                rc == COOPNET_OK);
            sCoopNetJoinStartTime = rc == COOPNET_OK ? time(NULL) : 0;
            switch_coopnet_log_printf("lobby join lobby_id=%" PRIu64 " rc=%d",
                                      gCoopNetDesiredLobby, (int)rc);
            switch_coopnet_log_printf("join recovery send result state=%s retry=%d",
                                      coopnet_switch_join_state_name(sCoopNetJoinRecovery.state),
                                      retry ? 1 : 0);
            switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_join", "AFTER");
            switch_crash_log_checkpoint("network: lobby join completed");
            if (joinAction == COOPNET_JOIN_RECOVERY_ACTION_FAIL) {
                sCoopNetJoinFailureReason = retry
                    ? "retry join request could not be sent"
                    : "join request could not be sent";
            }
#endif
        }
        sNetworkType = NT_NONE;
    }
#if defined(__SWITCH__) || defined(__3DS__)
    if (coopnet_join_recovery_failed(&sCoopNetJoinRecovery)) {
        coopnet_switch_finish_join_failure();
    }
#endif
}

static int ns_coopnet_network_send(u8 localIndex, void* address, u8* data, u16 dataLength) {
    if (!coopnet_is_connected()) { return 1; }
    u64 userId = coopnet_raw_get_id(localIndex);
    if (localIndex == 0 && address != NULL) { userId = *(u64*)address; }
    CoopNetRc rc = coopnet_send_to(userId, data, dataLength);
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_tx(dataLength, (int)rc);
    if (rc != COOPNET_OK) {
        switch_coopnet_log_printf("send failure peer_id=%" PRIu64 " rc=%d", userId, (int)rc);
        switch_coopnet_log_flush(true);
    }
#endif

    return 0;
}

static bool coopnet_allow_invite(void) {
    if (sLocalLobbyId == 0) { return false; }
    return (sLocalLobbyOwnerId == coopnet_get_local_user_id()) || (strlen(gCoopNetPassword) == 0);
}

static void ns_coopnet_get_lobby_id(UNUSED char* destination, UNUSED u32 destLength) {
    if (sLocalLobbyId == 0) {
        snprintf(destination, destLength, "%s", "");
    } else {
        snprintf(destination, destLength, "coopnet:%" PRIu64 "", sLocalLobbyId);
    }
}

static void ns_coopnet_get_lobby_secret(UNUSED char* destination, UNUSED u32 destLength) {
    if (sLocalLobbyId == 0 || !coopnet_allow_invite()) {
        snprintf(destination, destLength, "%s", "");
    } else {
        snprintf(destination, destLength, "coopnet:%" PRIu64":%s", sLocalLobbyId, gCoopNetPassword);
    }
}

static void ns_coopnet_shutdown(bool reconnecting) {
    if (reconnecting) { return; }
    LOG_INFO("Coopnet shutdown!");
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "BEFORE");
#endif
    CoopNetRc rc = coopnet_shutdown();
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("network shutdown rc=%d", (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "AFTER");
#endif
    gCoopNetCallbacks.OnLobbyListGot = NULL;
    gCoopNetCallbacks.OnLobbyListFinish = NULL;

    gCoopNetCallbacks.OnConnected = NULL;
    gCoopNetCallbacks.OnDisconnected = NULL;
    gCoopNetCallbacks.OnLobbyCreated = NULL;
    gCoopNetCallbacks.OnReceive = NULL;
    gCoopNetCallbacks.OnLobbyJoined = NULL;
    gCoopNetCallbacks.OnLobbyLeft = NULL;
    gCoopNetCallbacks.OnError = NULL;
    gCoopNetCallbacks.OnPeerConnected = NULL;
    gCoopNetCallbacks.OnPeerDisconnected = NULL;
    gCoopNetCallbacks.OnLoadBalance = NULL;

    sQueryCallback = NULL;
    sQueryFinishCallback = NULL;
    sLocalLobbyId = 0;
    sLocalLobbyOwnerId = 0;
#if defined(__SWITCH__) || defined(__3DS__)
    sPendingModListRequest = false;
    coopnet_switch_reset_join_recovery("network shutdown");
    coopnet_switch_clear_connected_peers();
    switch_coopnet_log_shutdown_summary();
#endif
}

static CoopNetRc coopnet_initialize(void) {
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_init();
#endif
#if defined(__3DS__)
    /* libjuice/CoopNet need sockets; SOC is deferred until multiplayer starts. */
    if (!new3ds_runtime_ensure_network() || !new3ds_runtime_network_available()) {
        switch_coopnet_log_printf("CoopNet blocked: SOC unavailable");
        switch_crash_log_checkpoint("network: CoopNet SOC unavailable");
        djui_popup_create(DLANG(NOTIF, COOPNET_CONNECTION_FAILED), 2);
        return COOPNET_FAILED;
    }
    switch_coopnet_log_printf("CoopNet SOC ready before coopnet_begin");
#endif
    gCoopNetCallbacks.OnConnected = coopnet_on_connected;
    gCoopNetCallbacks.OnDisconnected = coopnet_on_disconnected;
    gCoopNetCallbacks.OnLobbyCreated = coopnet_on_lobby_created;
    gCoopNetCallbacks.OnReceive = coopnet_on_receive;
    gCoopNetCallbacks.OnLobbyJoined = coopnet_on_lobby_joined;
    gCoopNetCallbacks.OnLobbyLeft = coopnet_on_lobby_left;
    gCoopNetCallbacks.OnError = coopnet_on_error;
    gCoopNetCallbacks.OnPeerConnected = coopnet_on_peer_connected;
    gCoopNetCallbacks.OnPeerDisconnected = coopnet_on_peer_disconnected;
    gCoopNetCallbacks.OnLoadBalance = coopnet_on_load_balance;

    if (coopnet_is_connected()) {
#if defined(__SWITCH__) || defined(__3DS__)
        switch_coopnet_log_printf("coopnet initialize reused existing signaling connection");
#endif
        return COOPNET_OK;
    }

    char* endptr = NULL;
    uint64_t destId = strtoull(configDestId, &endptr, 10);

#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("signaling target host=%s port=%u game=%s",
                              configCoopNetIp, (unsigned)configCoopNetPort, COOPNET_GAME_NAME);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_begin", "BEFORE");
#endif
    CoopNetRc rc = coopnet_begin(configCoopNetIp, configCoopNetPort, configPlayerName, destId);
#if defined(__SWITCH__) || defined(__3DS__)
    switch_coopnet_log_printf("coopnet_begin rc=%d", (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_begin", "AFTER");
#endif
    if (rc == COOPNET_FAILED) {
        djui_popup_create(DLANG(NOTIF, COOPNET_CONNECTION_FAILED), 2);
    }
    return rc;
}

struct NetworkSystem gNetworkSystemCoopNet = {
    .initialize       = ns_coopnet_initialize,
    .get_id           = ns_coopnet_get_id,
    .get_id_str       = ns_coopnet_get_id_str,
    .save_id          = ns_coopnet_save_id,
    .clear_id         = ns_coopnet_clear_id,
    .dup_addr         = ns_coopnet_dup_addr,
    .match_addr       = ns_coopnet_match_addr,
    .update           = ns_coopnet_update,
    .send             = ns_coopnet_network_send,
    .get_lobby_id     = ns_coopnet_get_lobby_id,
    .get_lobby_secret = ns_coopnet_get_lobby_secret,
    .shutdown         = ns_coopnet_shutdown,
    .requireServerBroadcast = false,
    .name             = "CoopNet",
};

#endif
