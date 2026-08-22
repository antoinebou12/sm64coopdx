#include <inttypes.h>
#include "libcoopnet.h"
#include "coopnet.h"
#include "coopnet_id.h"
#include "pc/network/network.h"
#include "pc/network/version.h"
#include "pc/djui/djui_language.h"
#include "pc/djui/djui_popup.h"
#include "pc/mods/mods.h"
#include "pc/utils/misc.h"
#include "pc/debuglog.h"
#ifdef __SWITCH__
#include "pc/platform/switch/switch_coopnet_log.h"
#endif
#ifdef DISCORD_SDK
#include "pc/discord/discord.h"
#endif

#ifdef COOPNET

#define MAX_COOPNET_DESCRIPTION_LENGTH 1024

uint64_t gCoopNetDesiredLobby = 0;
char gCoopNetPassword[64] = "";
char sCoopNetDescription[MAX_COOPNET_DESCRIPTION_LENGTH] = "";

static uint64_t sLocalLobbyId = 0;
static uint64_t sLocalLobbyOwnerId = 0;
static enum NetworkType sNetworkType;
static bool sReconnecting = false;
static QueryCallbackPtr sQueryCallback = NULL;
static QueryFinishCallbackPtr sQueryFinishCallback = NULL;
#ifdef __SWITCH__
static bool sPendingModListRequest = false;
#endif

static CoopNetRc coopnet_initialize(void);

static void coopnet_on_lobby_list_got(uint64_t lobbyId, uint64_t ownerId, uint16_t connections,
                                      uint16_t maxConnections, const char* game, const char* version,
                                      const char* hostName, const char* mode, const char* description) {
#ifdef __SWITCH__
    switch_coopnet_log_printf("lobby list item lobby_id=%" PRIu64 " owner_id=%" PRIu64
                              " connections=%u max_connections=%u",
                              lobbyId, ownerId, connections, maxConnections);
#endif
    if (sQueryCallback != NULL) {
        sQueryCallback(lobbyId, ownerId, connections, maxConnections, game, version, hostName, mode, description);
    }
}

static void coopnet_on_lobby_list_finish(void) {
#ifdef __SWITCH__
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
#ifdef __SWITCH__
        switch_coopnet_log_printf("lobby list initialize failed");
        switch_coopnet_log_flush(true);
#endif
        return false;
    }

    gCoopNetCallbacks.OnLobbyListGot = coopnet_on_lobby_list_got;
    gCoopNetCallbacks.OnLobbyListFinish = coopnet_on_lobby_list_finish;

#ifdef __SWITCH__
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_list_get", "BEFORE");
#endif
    CoopNetRc rc = coopnet_lobby_list_get(GAME_NAME, password);
#ifdef __SWITCH__
    switch_coopnet_log_printf("lobby list request rc=%d", (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_list_get", "AFTER");
#endif
    return rc == COOPNET_OK;
}

static void coopnet_on_connected(uint64_t userId) {
#ifdef __SWITCH__
    switch_coopnet_log_printf("signaling connected user_id=%" PRIu64, userId);
    switch_coopnet_log_flush(true);
#endif
    coopnet_set_local_user_id(userId);
}

static void coopnet_on_disconnected(bool intentional) {
    LOG_INFO("Coopnet shutdown!");
#ifdef __SWITCH__
    switch_coopnet_log_printf("signaling disconnected intentional=%d", intentional ? 1 : 0);
    switch_coopnet_log_flush(true);
#endif
    if (!intentional) {
        djui_popup_create(DLANG(NOTIF, COOPNET_DISCONNECTED), 2);
    }
#ifdef __SWITCH__
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "BEFORE");
#endif
    CoopNetRc rc = coopnet_shutdown();
#ifdef __SWITCH__
    switch_coopnet_log_printf("disconnect callback shutdown rc=%d", (int)rc);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "AFTER");
    sPendingModListRequest = false;
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
#ifdef __SWITCH__
    switch_coopnet_log_printf("lobby created lobby_id=%" PRIu64 " max_connections=%u",
                              lobbyId, maxConnections);
    switch_coopnet_log_flush(true);
#endif
}

static void coopnet_on_peer_connected(uint64_t peerId) {
#ifdef __SWITCH__
    switch_coopnet_log_printf("peer connected peer_id=%" PRIu64, peerId);
    if (sPendingModListRequest && gNetworkType == NT_CLIENT && peerId == sLocalLobbyOwnerId) {
        sPendingModListRequest = false;
        switch_coopnet_log_printf("peer ready, sending deferred mod list request peer_id=%" PRIu64, peerId);
        network_send_mod_list_request();
    }
    switch_coopnet_log_flush(true);
#else
    (void)peerId;
#endif
}

static void coopnet_on_peer_disconnected(uint64_t peerId) {
#ifdef __SWITCH__
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
#ifdef __SWITCH__
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
#ifdef __SWITCH__
    switch_coopnet_log_rx(dataLength);
#endif
    coopnet_set_user_id(0, userId);
    u8 localIndex = coopnet_user_id_to_local_index(userId);
    network_receive(localIndex, &userId, (u8*)data, dataLength);
}

static void coopnet_on_lobby_joined(uint64_t lobbyId, uint64_t userId, uint64_t ownerId, uint64_t destId) {
    LOG_INFO("coopnet_on_lobby_joined!");
#ifdef __SWITCH__
    switch_coopnet_log_printf("lobby joined lobby_id=%" PRIu64 " user_id=%" PRIu64
                              " owner_id=%" PRIu64,
                              lobbyId, userId, ownerId);
    switch_coopnet_log_flush(true);
#endif
    coopnet_set_user_id(0, ownerId);
    sLocalLobbyId = lobbyId;
    sLocalLobbyOwnerId = ownerId;

    if (userId == coopnet_get_local_user_id()) {
        coopnet_clear_dest_ids();
        snprintf(configDestId, MAX_CONFIG_STRING, "%" PRIu64 "", destId);
    }

    coopnet_save_dest_id(userId, destId);

    if (userId == coopnet_get_local_user_id() && gNetworkType == NT_CLIENT) {
#ifdef __SWITCH__
        /* libcoopnet reports lobby membership before libjuice has nominated an
         * ICE pair. Sending here produces COOPNET_FAILED until OnPeerConnected
         * and caused the repeated 26-byte failures seen on hardware. */
        sPendingModListRequest = true;
        switch_coopnet_log_printf("mod list request deferred until ICE peer connection owner_id=%" PRIu64,
                                  ownerId);
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
#ifdef __SWITCH__
    switch_coopnet_log_printf("lobby left lobby_id=%" PRIu64 " user_id=%" PRIu64,
                              lobbyId, userId);
    if (userId == coopnet_get_local_user_id()) {
        sPendingModListRequest = false;
    }
    switch_coopnet_log_flush(true);
#endif
    coopnet_clear_dest_id(userId);
    if (lobbyId == sLocalLobbyId && userId == coopnet_get_local_user_id()) {
        network_shutdown(false, false, true, false);
    }
}

static void coopnet_on_error(enum MPacketErrorNumber error, uint64_t tag) {
#ifdef __SWITCH__
    switch_coopnet_log_printf("coopnet error=%d tag=%" PRIu64, (int)error, tag);
    switch_coopnet_log_flush(true);
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
#ifdef __SWITCH__
    switch_coopnet_log_init();
    switch_coopnet_log_printf("network initialize type=%d reconnecting=%d",
                              (int)networkType, reconnecting ? 1 : 0);
    if (!reconnecting && networkType != NT_CLIENT) {
        sPendingModListRequest = false;
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
    // get version
    const char* version = get_version();
    int versionLength = strlen(version);
    snprintf(buffer, bufferLength, "%s", version);
    buffer += versionLength;
    bufferLength -= versionLength;

    // get mod strings
    if (gActiveMods.entryCount <= 0) { return; }
    char* strings[gActiveMods.entryCount];
    for (int i = 0; i < gActiveMods.entryCount; i++) {
        struct Mod* mod = gActiveMods.entries[i];
        strings[i] = mod->name;
    }

    // add seperator
    char* sep = "\n\nMods:\n";
    snprintf(buffer, bufferLength, "%s", sep);
    buffer += strlen(sep);
    bufferLength -= strlen(sep);

    // concat mod strings
    str_seperator_concat(buffer, bufferLength, strings, gActiveMods.entryCount, "\\#\\\n");
}

void ns_coopnet_update(void) {
    if (!coopnet_is_connected()) { return; }

    CoopNetRc updateRc = coopnet_update();
#ifdef __SWITCH__
    if (updateRc != COOPNET_OK) {
        switch_coopnet_log_printf("coopnet_update rc=%d", (int)updateRc);
        switch_coopnet_log_flush(true);
    }
#endif
    if (gNetworkType != NT_NONE && sNetworkType != NT_NONE) {
        if (sNetworkType == NT_SERVER) {
            char mode[64] = "";
            mods_get_main_mod_name(mode, 64);
            if (sReconnecting) {
                LOG_INFO("Update lobby");
                coopnet_populate_description();
#ifdef __SWITCH__
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_update", "BEFORE");
#endif
                CoopNetRc rc = coopnet_lobby_update(sLocalLobbyId, GAME_NAME, get_version(), configPlayerName, mode, sCoopNetDescription);
#ifdef __SWITCH__
                switch_coopnet_log_printf("lobby update lobby_id=%" PRIu64 " rc=%d", sLocalLobbyId, (int)rc);
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_update", "AFTER");
#endif
            } else {
                LOG_INFO("Create lobby");
                snprintf(gCoopNetPassword, 64, "%s", configPassword);
                coopnet_populate_description();
#ifdef __SWITCH__
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_create", "BEFORE");
#endif
                CoopNetRc rc = coopnet_lobby_create(GAME_NAME, get_version(), configPlayerName, mode,
                                                     (uint16_t)configAmountOfPlayers, gCoopNetPassword,
                                                     sCoopNetDescription);
#ifdef __SWITCH__
                switch_coopnet_log_printf("lobby create max_players=%u rc=%d",
                                          (unsigned)configAmountOfPlayers, (int)rc);
                switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_create", "AFTER");
#endif
            }
        } else if (sNetworkType == NT_CLIENT) {
            LOG_INFO("Join lobby");
#ifdef __SWITCH__
            switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_join", "BEFORE");
#endif
            CoopNetRc rc = coopnet_lobby_join(gCoopNetDesiredLobby, gCoopNetPassword);
#ifdef __SWITCH__
            switch_coopnet_log_printf("lobby join lobby_id=%" PRIu64 " rc=%d",
                                      gCoopNetDesiredLobby, (int)rc);
            switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_lobby_join", "AFTER");
#endif
        }
        sNetworkType = NT_NONE;
    }
}

static int ns_coopnet_network_send(u8 localIndex, void* address, u8* data, u16 dataLength) {
    if (!coopnet_is_connected()) { return 1; }
    //if (gCurLobbyId == 0) { return 2; }
    u64 userId = coopnet_raw_get_id(localIndex);
    if (localIndex == 0 && address != NULL) { userId = *(u64*)address; }
    CoopNetRc rc = coopnet_send_to(userId, data, dataLength);
#ifdef __SWITCH__
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
#ifdef __SWITCH__
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_shutdown", "BEFORE");
#endif
    CoopNetRc rc = coopnet_shutdown();
#ifdef __SWITCH__
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
#ifdef __SWITCH__
    sPendingModListRequest = false;
    switch_coopnet_log_shutdown_summary();
#endif
}

static CoopNetRc coopnet_initialize(void) {
#ifdef __SWITCH__
    switch_coopnet_log_init();
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
#ifdef __SWITCH__
        switch_coopnet_log_printf("coopnet initialize reused existing signaling connection");
#endif
        return COOPNET_OK;
    }

    char* endptr = NULL;
    uint64_t destId = strtoull(configDestId, &endptr, 10);

#ifdef __SWITCH__
    switch_coopnet_log_printf("signaling target host=%s port=%u",
                              configCoopNetIp, (unsigned)configCoopNetPort);
    switch_coopnet_log_checkpoint("LIBCOOPNET", "coopnet_begin", "BEFORE");
#endif
    CoopNetRc rc = coopnet_begin(configCoopNetIp, configCoopNetPort, configPlayerName, destId);
#ifdef __SWITCH__
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