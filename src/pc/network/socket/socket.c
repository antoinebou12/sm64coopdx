#include "socket.h"
#include <stdio.h>
#include "pc/configfile.h"
#include "pc/debuglog.h"
#include "pc/djui/djui.h"

#ifdef __SWITCH__
typedef struct sockaddr_in NetworkSocketAddr;
#define NETWORK_SOCKET_FAMILY AF_INET
#define NETWORK_SOCKET_ADDR_LEN INET_ADDRSTRLEN
#else
typedef struct sockaddr_in6 NetworkSocketAddr;
#define NETWORK_SOCKET_FAMILY AF_INET6
#define NETWORK_SOCKET_ADDR_LEN INET6_ADDRSTRLEN
#endif

static SOCKET sCurSocket = INVALID_SOCKET;
static NetworkSocketAddr sAddr[MAX_PLAYERS] = { 0 };
static struct addrinfo hints;
static struct addrinfo *result, *i;
#ifdef __SWITCH__
static bool sSwitchLocalOnly = false;
#endif

char gGetHostName[MAX_CONFIG_STRING] = "";

static bool resolve_domain(NetworkSocketAddr *addr) {
    int error;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
#ifdef __SWITCH__
    // Horizon direct-connect deliberately stays on IPv4. This avoids the
    // IPv4-mapped IPv6/dual-stack path that proved unreliable on Switch.
    hints.ai_family = AF_INET;
#else
    hints.ai_family = AF_UNSPEC;

    if (configJoinIp[0] == '[') {
        LOG_INFO("sanity check: found opening square bracket on configJoinIp, removing it.");
        for (int i = 0; i < MAX_CONFIG_STRING; i++) {
            if (configJoinIp[i] == '\0') { break; }
            if (configJoinIp[i] == ']') {
                configJoinIp[i] = '\0';
                memcpy(&configJoinIp, &configJoinIp[1], MAX_CONFIG_STRING - 1);
                break;
            }
        }
    }
#endif

#ifdef __SWITCH__
    LOG_INFO("Switch Direct: DNS begin host=%s", configJoinIp);
#endif
    error = getaddrinfo(configJoinIp, NULL, &hints, &result);
    if (error != 0) {
        LOG_ERROR("getaddrinfo() failed with error code %i: %s", error, gai_strerror(error));
#ifdef __SWITCH__
        LOG_ERROR("Switch Direct: DNS failed host=%s rc=%d", configJoinIp, error);
#endif
        return false;
    }

    for (i = result; i != NULL; i = i->ai_next) {
        char str[NETWORK_SOCKET_ADDR_LEN];
#ifdef __SWITCH__
        if (i->ai_addr->sa_family != AF_INET) { continue; }
        struct sockaddr_in *p = (struct sockaddr_in *)i->ai_addr;
        addr->sin_family = AF_INET;
        memcpy(&addr->sin_addr, &p->sin_addr, sizeof(struct in_addr));
        snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", inet_ntop(AF_INET, &p->sin_addr, str, sizeof(str)));
        LOG_INFO("Switch Direct: DNS resolved ipv4=%s", configJoinIp);
        freeaddrinfo(result);
        result = NULL;
        return true;
#else
        if (i->ai_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *p = (struct sockaddr_in6 *)i->ai_addr;
            memcpy(&addr->sin6_addr, &p->sin6_addr, sizeof(struct in6_addr));
            snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", inet_ntop(AF_INET6, &p->sin6_addr, str, sizeof(str)));
            freeaddrinfo(result);
            result = NULL;
            return true;
        } else if (i->ai_addr->sa_family == AF_INET) {
            struct sockaddr_in *p = (struct sockaddr_in *)i->ai_addr;
            struct in6_addr ipv6_mapped_addr;
            memset(&ipv6_mapped_addr, 0, sizeof(struct in6_addr));
            ipv6_mapped_addr.s6_addr[10] = 0xff;
            ipv6_mapped_addr.s6_addr[11] = 0xff;
            memcpy(&ipv6_mapped_addr.s6_addr[12], &p->sin_addr, sizeof(p->sin_addr));
            memcpy(&addr->sin6_addr, &ipv6_mapped_addr, sizeof(struct in6_addr));
            snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", inet_ntop(AF_INET6, &ipv6_mapped_addr, str, sizeof(str)));
            freeaddrinfo(result);
            result = NULL;
            return true;
        }
#endif
    }

    freeaddrinfo(result);
    result = NULL;
    LOG_ERROR("getaddrinfo() returned no compatible address for %s", configJoinIp);
    return false;
}

static int socket_bind(SOCKET socket, unsigned int port) {
    NetworkSocketAddr rxAddr;
    memset(&rxAddr, 0, sizeof(rxAddr));
#ifdef __SWITCH__
    rxAddr.sin_family = AF_INET;
    rxAddr.sin_port = htons(port);
    rxAddr.sin_addr.s_addr = htonl(INADDR_ANY);
#else
    rxAddr.sin6_family = AF_INET6;
    rxAddr.sin6_port = htons(port);
    rxAddr.sin6_addr = in6addr_any;
#endif

    int rc = bind(socket, (SOCKADDR *)&rxAddr, sizeof(rxAddr));
    if (rc != 0) {
        LOG_ERROR("bind failed with error %d", SOCKET_LAST_ERROR);
#ifdef __SWITCH__
        LOG_ERROR("Switch Direct: bind failed port=%u error=%d", port, SOCKET_LAST_ERROR);
#endif
    }
    return rc;
}

static int socket_send(SOCKET socket, NetworkSocketAddr* addr, u8* buffer, u16 bufferLength) {
    int addrSize = sizeof(NetworkSocketAddr);
    int rc = sendto(socket, (char*)buffer, bufferLength, 0, (struct sockaddr*)addr, addrSize);
    if (rc != SOCKET_ERROR) { return NO_ERROR; }

    int error = SOCKET_LAST_ERROR;
    if (error == SOCKET_EWOULDBLOCK) { return NO_ERROR; }

    LOG_ERROR("sendto failed with error: %d", error);
#ifdef __SWITCH__
    LOG_ERROR("Switch Direct: sendto failed bytes=%u error=%d", bufferLength, error);
#endif
    return rc;
}

static int socket_receive(SOCKET socket, NetworkSocketAddr* rxAddr, u8* buffer, u16 bufferLength, u16* receiveLength, u8* localIndex) {
    *receiveLength = 0;

    RX_ADDR_SIZE_TYPE rxAddrSize = sizeof(NetworkSocketAddr);
    int rc = recvfrom(socket, (char*)buffer, bufferLength, 0, (struct sockaddr*)rxAddr, &rxAddrSize);

    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (memcmp(rxAddr, &sAddr[i], sizeof(NetworkSocketAddr)) == 0) {
            *localIndex = i;
            break;
        }
    }

    if (rc == SOCKET_ERROR) {
        int error = SOCKET_LAST_ERROR;
        if (error != SOCKET_EWOULDBLOCK && error != SOCKET_ECONNRESET) {
            LOG_ERROR("recvfrom failed with error %d", SOCKET_LAST_ERROR);
#ifdef __SWITCH__
            LOG_ERROR("Switch Direct: recvfrom failed error=%d", SOCKET_LAST_ERROR);
#endif
        }
        return SOCKET_ERROR;
    }

    *receiveLength = rc;
    return NO_ERROR;
}

static bool ns_socket_initialize(enum NetworkType networkType, UNUSED bool reconnecting) {
    unsigned int port = (networkType == NT_CLIENT) ? configJoinPort : configHostPort;
    if (port == 0) { port = DEFAULT_PORT; }

#ifdef __SWITCH__
    sSwitchLocalOnly = (networkType == NT_SERVER && configAmountOfPlayers == 1);
    if (sSwitchLocalOnly) {
        sCurSocket = INVALID_SOCKET;
        memset(sAddr, 0, sizeof(sAddr));
        LOG_INFO("Switch Solo: local-only host initialized without UDP socket");
        return true;
    }
    LOG_INFO("Switch Direct: socket initialize type=%d port=%u", (int)networkType, port);
#endif

    sCurSocket = socket_initialize();
    if (sCurSocket == INVALID_SOCKET) {
#ifdef __SWITCH__
        LOG_ERROR("Switch Direct: socket_initialize failed");
#endif
        return false;
    }

    if (networkType == NT_SERVER) {
        int reuse = 1;
        if (setsockopt(sCurSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
            LOG_ERROR("setsockopt(SO_REUSEADDR) failed");
        }

#ifdef SO_REUSEPORT
        if (setsockopt(sCurSocket, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0) {
            LOG_ERROR("setsockopt(SO_REUSEPORT) failed");
        }
#endif
        int rc = socket_bind(sCurSocket, port);
        if (rc != NO_ERROR) {
            LOG_ERROR("bind returned an error.");
            socket_shutdown(sCurSocket);
            sCurSocket = INVALID_SOCKET;
            return false;
        }
        LOG_INFO("bound to port %u", port);
#ifdef __SWITCH__
        LOG_INFO("Switch Direct: host listening ipv4 port=%u", port);
#endif
    } else if (networkType == NT_CLIENT) {
        NetworkSocketAddr addr;
        memset(&addr, 0, sizeof(addr));
#ifdef __SWITCH__
        sAddr[0].sin_family = AF_INET;
        sAddr[0].sin_port = htons(port);
#else
        sAddr[0].sin6_family = AF_INET6;
        sAddr[0].sin6_port = htons(port);
#endif
        if (!resolve_domain(&addr)) {
            socket_shutdown(sCurSocket);
            sCurSocket = INVALID_SOCKET;
            return false;
        }
#ifdef __SWITCH__
        sAddr[0].sin_addr = addr.sin_addr;
#else
        sAddr[0].sin6_addr = addr.sin6_addr;
#endif
        LOG_INFO("connecting to %s, port %u", configJoinIp, port);
#ifdef __SWITCH__
        LOG_INFO("Switch Direct: client target ipv4=%s port=%u", configJoinIp, port);
#endif
        snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", gGetHostName);

        char joinText[128] = { 0 };
        snprintf(joinText, 63, "%s %d", configJoinIp, configJoinPort);
        djui_connect_menu_open();

        gNetworkType = NT_CLIENT;
    }

    LOG_INFO("initialized");

    if (networkType == NT_CLIENT) {
#ifdef __SWITCH__
        LOG_INFO("Switch Direct: sending initial mod-list request");
#endif
        network_send_mod_list_request();
    }

    return true;
}

static s64 ns_socket_get_id(UNUSED u8 localId) {
    return 0;
}

static char* ns_socket_get_id_str(u8 localId) {
    if (localId == UNKNOWN_LOCAL_INDEX) { localId = 0; }
    static char id_str[NETWORK_SOCKET_ADDR_LEN] = { 0 };
#ifdef __SWITCH__
    inet_ntop(AF_INET, &sAddr[localId].sin_addr, id_str, sizeof(id_str));
#else
    inet_ntop(AF_INET6, &sAddr[localId].sin6_addr, id_str, sizeof(id_str));
#endif
    return id_str;
}

static void ns_socket_save_id(u8 localId, UNUSED s64 networkId) {
    SOFT_ASSERT(localId > 0);
    SOFT_ASSERT(localId < MAX_PLAYERS);
    sAddr[localId] = sAddr[0];
    LOG_INFO("saved addr for id %d", localId);
}

static void ns_socket_clear_id(u8 localId) {
    if (localId == 0) { return; }
    SOFT_ASSERT(localId < MAX_PLAYERS);
    memset(&sAddr[localId], 0, sizeof(NetworkSocketAddr));
    LOG_INFO("cleared addr for id %d", localId);
}

static void* ns_socket_dup_addr(u8 localIndex) {
    void* address = malloc(sizeof(NetworkSocketAddr));
    memcpy(address, &sAddr[localIndex], sizeof(NetworkSocketAddr));
    return address;
}

static bool ns_socket_match_addr(void* addr1, void* addr2) {
    return !memcmp(addr1, addr2, sizeof(NetworkSocketAddr));
}

static void ns_socket_update(void) {
#ifdef __SWITCH__
    if (sSwitchLocalOnly) { return; }
#endif
    if (gNetworkType == NT_NONE || sCurSocket == INVALID_SOCKET) { return; }
    do {
        u8 data[PACKET_LENGTH + 1];
        u16 dataLength = 0;
        u8 localIndex = UNKNOWN_LOCAL_INDEX;
        int rc = socket_receive(sCurSocket, &sAddr[0], data, PACKET_LENGTH + 1, &dataLength, &localIndex);
        SOFT_ASSERT(dataLength < PACKET_LENGTH);
        if (rc != NO_ERROR) { break; }
        network_receive(localIndex, &sAddr[0], data, dataLength);
    } while (true);
}

static int ns_socket_send(u8 localIndex, void* address, u8* data, u16 dataLength) {
#ifdef __SWITCH__
    if (sSwitchLocalOnly) { return NO_ERROR; }
#endif
    if (sCurSocket == INVALID_SOCKET) { return SOCKET_ERROR; }
    if (localIndex != 0) {
        if (gNetworkType == NT_SERVER && gNetworkPlayers[localIndex].type != NPT_CLIENT) { return SOCKET_ERROR; }
        if (gNetworkType == NT_CLIENT && gNetworkPlayers[localIndex].type != NPT_SERVER) { return SOCKET_ERROR; }
    }

    NetworkSocketAddr* userAddr = &sAddr[localIndex];
    if (localIndex == 0 && address != NULL) { userAddr = (NetworkSocketAddr*)address; }

    int rc = socket_send(sCurSocket, userAddr, data, dataLength);
    if (rc) {
        LOG_ERROR("    localIndex: %d, packetType: %d, dataLength: %d", localIndex, data[0], dataLength);
    }
    return rc;
}

static void ns_socket_get_lobby_id(char* destination, u32 destLength) {
    snprintf(destination, destLength, "%s", "");
}

static void ns_socket_get_lobby_secret(char* destination, u32 destLength) {
    snprintf(destination, destLength, "%s", "");
}

static void ns_socket_shutdown(UNUSED bool reconnecting) {
    socket_shutdown(sCurSocket);
    sCurSocket = INVALID_SOCKET;
    for (u16 i = 0; i < MAX_PLAYERS; i++) {
        memset(&sAddr[i], 0, sizeof(NetworkSocketAddr));
    }
#ifdef __SWITCH__
    sSwitchLocalOnly = false;
#endif
    LOG_INFO("shutdown");
}

struct NetworkSystem gNetworkSystemSocket = {
    .initialize       = ns_socket_initialize,
    .get_id           = ns_socket_get_id,
    .get_id_str       = ns_socket_get_id_str,
    .save_id          = ns_socket_save_id,
    .clear_id         = ns_socket_clear_id,
    .dup_addr         = ns_socket_dup_addr,
    .match_addr       = ns_socket_match_addr,
    .update           = ns_socket_update,
    .send             = ns_socket_send,
    .get_lobby_id     = ns_socket_get_lobby_id,
    .get_lobby_secret = ns_socket_get_lobby_secret,
    .shutdown         = ns_socket_shutdown,
    .requireServerBroadcast = true,
    .name             = "Socket",
};
