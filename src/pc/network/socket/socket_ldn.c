#ifdef __SWITCH__

// Isolated on purpose: this file includes ONLY <switch.h> (libnx),
// standard/socket headers and socket_ldn_util.h, never a project header that
// reaches PR/ultratypes.h. <switch.h>'s u64/s64 typedefs (long) conflict with
// this project's own u64/s64 (long long) - same width, but C treats them as
// distinct types, so the two cannot be included in the same translation unit.
// socket_ldn_util.h is deliberately free of those typedefs. The NetworkSystem
// glue lives in socket_ldn_glue.c and calls the plain-C-typed functions
// below instead of touching libnx types directly.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <switch.h>

#include "socket_ldn_util.h"

// LDN is only the association/discovery layer. Game packets use ordinary
// UDP over the IPv4 subnet assigned by the LDN service. This avoids the
// action-frame payload limit and keeps the hot data path in BSD sockets.
#define LDN_UDP_PORT 7777

extern void network_receive(unsigned char localIndex, void* addr, unsigned char* data, unsigned short dataLength);
extern char configPlayerName[];

static void ldn_fill_user_name(char* dst, unsigned int dstLen) {
    ldn_util_sanitize_user_name(configPlayerName, dst, dstLen);
}

static bool sLdnInitialized = false;
static bool sLdnAccessPointOpen = false;
static bool sLdnStationOpen = false;
static bool sLdnConnected = false;
static bool sIsServer = false;
static LdnNetworkInfo sLdnNetworkInfo[4];
static int sLdnNetworkCount = 0;

static int sUdpSocket = -1;
static struct in_addr sOwnAddr;
static LdnPeerTable sPeers;

static int sSendOkCount = 0;
static int sSendFailCount = 0;
static int sRecvOkCount = 0;
static int sConnectedCount = 0;
static u64 sLastHeartbeatTick = 0;

void ldn_shutdown_impl(void);

static void ldn_log(const char* fmt, ...) {
    static int sCallsRemaining = 2000;
    if (sCallsRemaining <= 0) { return; }
    static bool sOpened = false;
    FILE* f = fopen("sdmc:/sm64coopdx_ldn.log", sOpened ? "a" : "w");
    sOpened = true;
    sCallsRemaining--;

    if (!f) { return; }
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

static uint32_t ldn_to_net_addr(LdnIpv4Address ldnAddr) {
    return ldn_util_addr_to_network_order(ldnAddr.addr);
}

static void ldn_refresh_nodes(void) {
    LdnNetworkInfo netInfo;
    Result rc = ldnGetNetworkInfo(&netInfo);
    if (R_FAILED(rc)) {
        ldn_log("[LDN] ldnGetNetworkInfo failed 0x%x (mod=%04x desc=%04x)", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        return;
    }

    int connectedCount = 0;
    for (s32 i = 0; i < netInfo.node_count && i < 8; i++) {
        if (netInfo.nodes[i].is_connected) { connectedCount++; }
    }
    if (connectedCount != sConnectedCount) {
        ldn_log("[LDN] connected nodes %d -> %d", sConnectedCount, connectedCount);
        sConnectedCount = connectedCount;
    }

    if (!sIsServer && netInfo.node_count >= 1 && netInfo.nodes[0].is_connected) {
        sPeers.addr[0] = ldn_to_net_addr(netInfo.nodes[0].ip_addr);
    }
}

static bool ldn_udp_open(void) {
    // switch_platform.c owns socketInitializeDefault()/socketExit(). Calling
    // socketInitializeDefault() again here returns LibnxError_AlreadyInitialized
    // on current libnx, so this backend only consumes the already-open service.
    LdnIpv4Address ownLdnAddr;
    LdnSubnetMask mask;
    Result rc = ldnGetIpv4Address(&ownLdnAddr, &mask);
    if (R_FAILED(rc)) {
        ldn_log("[LDN] ldnGetIpv4Address failed 0x%x (mod=%04x desc=%04x)", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        return false;
    }
    sOwnAddr.s_addr = ldn_to_net_addr(ownLdnAddr);

    sUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sUdpSocket < 0) {
        ldn_log("[LDN] socket() failed: errno=%d", errno);
        return false;
    }

    int flags = fcntl(sUdpSocket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sUdpSocket, F_SETFL, flags | O_NONBLOCK);
    }

    int reuse = 1;
    setsockopt(sUdpSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(LDN_UDP_PORT);
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sUdpSocket, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        ldn_log("[LDN] bind() failed: errno=%d", errno);
        close(sUdpSocket);
        sUdpSocket = -1;
        return false;
    }

    ldn_util_peers_reset(&sPeers);
    sConnectedCount = 0;
    ldn_refresh_nodes();
    sLastHeartbeatTick = svcGetSystemTick();
    sSendOkCount = sSendFailCount = sRecvOkCount = 0;
    ldn_log("[LDN] UDP transport up on port %d (%s)", LDN_UDP_PORT, sIsServer ? "host" : "client");
    return true;
}

static void ldn_udp_close(void) {
    if (sUdpSocket >= 0) {
        close(sUdpSocket);
        sUdpSocket = -1;
    }
    ldn_util_peers_reset(&sPeers);
}

bool ldn_initialize_impl(bool isServer) {
    // A CoopDX-level reconnect/rehost deliberately preserves the physical LDN
    // association and UDP socket. In that case there is nothing to recreate.
    if (sLdnInitialized && sLdnConnected && sIsServer == isServer) { return true; }
    sIsServer = isServer;

    if (sLdnInitialized && ((isServer && sLdnStationOpen) || (!isServer && sLdnAccessPointOpen))) {
        ldn_shutdown_impl();
        sIsServer = isServer;
    }

    if (!sLdnInitialized) {
        Result rc = 0;
        for (int attempt = 0; attempt < 5; attempt++) {
            rc = ldnInitialize(LdnServiceType_User);
            ldn_log("[LDN] ldnInitialize attempt=%d: 0x%x", attempt, rc);
            if (R_SUCCEEDED(rc)) { break; }
            svcSleepThread(300000000ULL);
        }
        if (R_FAILED(rc)) { return false; }
        sLdnInitialized = true;
    }

    if (isServer) {
        if (!sLdnAccessPointOpen) {
            Result rc = ldnOpenAccessPoint();
            ldn_log("[LDN] ldnOpenAccessPoint: 0x%x", rc);
            if (R_FAILED(rc)) { return false; }
            sLdnAccessPointOpen = true;
        }

        LdnSecurityConfig sec;
        memset(&sec, 0, sizeof(sec));
        sec.security_mode = LdnSecurityMode_Any;
        sec.passphrase_size = 0x10;
        memset(sec.passphrase, 0x42, 0x10);

        LdnUserConfig user;
        memset(&user, 0, sizeof(user));
        ldn_fill_user_name(user.user_name, sizeof(user.user_name));

        LdnNetworkConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.intent_id.local_communication_id = -1;
        cfg.channel = 0;
        cfg.node_count_max = 4;

        Result rc = ldnCreateNetwork(&sec, &user, &cfg);
        ldn_log("[LDN] ldnCreateNetwork: 0x%x", rc);
        if (R_FAILED(rc)) {
            ldnCloseAccessPoint();
            sLdnAccessPointOpen = false;
            return false;
        }

        sLdnConnected = true;
        rc = ldnSetStationAcceptPolicy(LdnAcceptPolicy_AlwaysAccept);
        if (R_FAILED(rc)) {
            ldn_log("[LDN] ldnSetStationAcceptPolicy failed: 0x%x", rc);
            ldn_shutdown_impl();
            return false;
        }
        if (!ldn_udp_open()) {
            ldn_shutdown_impl();
            return false;
        }
    } else if (!sLdnStationOpen) {
        Result rc = ldnOpenStation();
        ldn_log("[LDN] ldnOpenStation: 0x%x", rc);
        if (R_FAILED(rc)) { return false; }
        sLdnStationOpen = true;
    }

    return true;
}

void ldn_update_impl(void) {
    if (!sLdnInitialized || !sLdnConnected || sUdpSocket < 0) { return; }

    u64 nowTick = svcGetSystemTick();
    u64 tickFreq = armGetSystemTickFreq();
    if ((nowTick - sLastHeartbeatTick) >= (tickFreq * 2)) {
        ldn_refresh_nodes();
        ldn_log("[LDN] heartbeat sendOk=%d sendFail=%d recvOk=%d connected=%d", sSendOkCount, sSendFailCount, sRecvOkCount, sConnectedCount);
        sSendOkCount = sSendFailCount = sRecvOkCount = 0;
        sLastHeartbeatTick = nowTick;
    }

    unsigned char recvBuf[LDN_UTIL_PACKET_LENGTH + 1];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        ssize_t len = recvfrom(sUdpSocket, recvBuf, LDN_UTIL_PACKET_LENGTH, 0, (struct sockaddr*)&from, &fromLen);
        if (len <= 0) { break; }
        if (!ldn_util_recv_length_valid((long)len)) {
            ldn_log("[LDN] dropping oversized datagram: %ld", (long)len);
            continue;
        }
        sRecvOkCount++;

        sPeers.addr[0] = from.sin_addr.s_addr;
        unsigned char localIndex = ldn_util_peer_index_for_addr(&sPeers, from.sin_addr.s_addr);
        network_receive(localIndex, &sPeers.addr[0], recvBuf, (unsigned short)len);
    }
}

int ldn_send_impl(unsigned char localIndex, void* address, unsigned char* data, unsigned short dataLength) {
    if (!sLdnInitialized || !sLdnConnected || sUdpSocket < 0) { return -1; }
    if (!ldn_util_send_length_valid(localIndex, dataLength)) { return -1; }

    uint32_t dest = sPeers.addr[localIndex];
    if (localIndex == 0 && address != NULL) {
        dest = *(const uint32_t*)address;
    }
    if (dest == 0) { return -1; }

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(LDN_UDP_PORT);
    to.sin_addr.s_addr = dest;

    ssize_t sent = sendto(sUdpSocket, data, dataLength, 0, (struct sockaddr*)&to, sizeof(to));
    if (sent < 0 || sent != dataLength) {
        sSendFailCount++;
        return -1;
    }
    sSendOkCount++;
    return 0;
}

void* ldn_dup_addr_impl(unsigned char localIndex) {
    if (localIndex >= LDN_UTIL_MAX_PLAYERS) { localIndex = 0; }
    uint32_t* copy = malloc(sizeof(uint32_t));
    if (copy) { *copy = sPeers.addr[localIndex]; }
    return copy;
}

bool ldn_match_addr_impl(void* a, void* b) {
    if (a == NULL || b == NULL) { return false; }
    return ldn_util_addr_equal(*(const uint32_t*)a, *(const uint32_t*)b);
}

void ldn_save_id_impl(unsigned char localIndex) {
    ldn_util_peer_save(&sPeers, localIndex);
}

void ldn_clear_id_impl(unsigned char localIndex) {
    ldn_util_peer_clear(&sPeers, localIndex);
}

void ldn_prepare_reconnect_impl(void) {
    if (!sLdnInitialized || !sLdnConnected || sUdpSocket < 0) { return; }

    // CoopDX player slots are rebuilt after a rehost/rejoin, while the local
    // wireless association itself remains valid. Drop stale slot bindings but
    // retain the client's host address in slot 0.
    ldn_util_peers_clear_for_reconnect(&sPeers, sIsServer);
    if (!sIsServer) {
        ldn_refresh_nodes();
    }
    sSendOkCount = sSendFailCount = sRecvOkCount = 0;
    ldn_log("[LDN] preserving physical association for CoopDX reconnect");
}

void ldn_shutdown_impl(void) {
    ldn_udp_close();
    if (!sLdnInitialized) { return; }

    const bool wasConnected = sLdnConnected;
    if (wasConnected && !sIsServer) {
        Result rc = ldnDisconnect();
        if (R_FAILED(rc)) { ldn_log("[LDN] ldnDisconnect failed: 0x%x", rc); }
    }
    sLdnConnected = false;

    if (sLdnStationOpen) {
        Result rc = ldnCloseStation();
        if (R_FAILED(rc)) { ldn_log("[LDN] ldnCloseStation failed: 0x%x", rc); }
        sLdnStationOpen = false;
    }
    if (sLdnAccessPointOpen) {
        if (wasConnected && sIsServer) {
            Result rc = ldnDestroyNetwork();
            if (R_FAILED(rc)) { ldn_log("[LDN] ldnDestroyNetwork failed: 0x%x", rc); }
        }
        Result rc = ldnCloseAccessPoint();
        if (R_FAILED(rc)) { ldn_log("[LDN] ldnCloseAccessPoint failed: 0x%x", rc); }
        sLdnAccessPointOpen = false;
    }
    ldnExit();
    sLdnInitialized = false;
    sLdnNetworkCount = 0;
    sConnectedCount = 0;
}

bool ldn_connect_to_index(int index) {
    if (index < 0 || index >= sLdnNetworkCount) { return false; }

    if (sLdnAccessPointOpen) {
        ldn_shutdown_impl();
    }

    sIsServer = false;
    if (!sLdnInitialized) {
        if (!ldn_initialize_impl(false)) { return false; }
    } else if (!sLdnStationOpen) {
        Result rc = ldnOpenStation();
        if (R_FAILED(rc)) { return false; }
        sLdnStationOpen = true;
    }

    LdnSecurityConfig sec;
    memset(&sec, 0, sizeof(sec));
    sec.security_mode = LdnSecurityMode_Any;
    sec.passphrase_size = 0x10;
    memset(sec.passphrase, 0x42, 0x10);

    LdnUserConfig user;
    memset(&user, 0, sizeof(user));
    ldn_fill_user_name(user.user_name, sizeof(user.user_name));

    // A re-scan may reorder the result array. Pin retries to the host BSSID
    // selected by the user rather than blindly reusing the old array index.
    u8 selectedBssid[sizeof(sLdnNetworkInfo[0].common.bssid.addr)];
    memcpy(selectedBssid, sLdnNetworkInfo[index].common.bssid.addr, sizeof(selectedBssid));

    Result rc = 0;
    bool connected = false;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (index < 0 || index >= sLdnNetworkCount) { break; }
        LdnNetworkInfo* target = &sLdnNetworkInfo[index];
        rc = ldnConnect(&sec, &user, 0, 0, target);
        ldn_log("[LDN] ldnConnect attempt=%d: 0x%x", attempt, rc);
        if (R_SUCCEEDED(rc)) { connected = true; break; }

        svcSleepThread(400000000ULL);
        LdnScanFilter filter;
        memset(&filter, 0, sizeof(filter));
        filter.network_id.intent_id.local_communication_id = -1;
        filter.flags = LdnScanFilterFlag_LocalCommunicationId;
        rc = ldnScan(0, &filter, sLdnNetworkInfo, 4, &sLdnNetworkCount);
        if (R_FAILED(rc)) {
            ldn_log("[LDN] retry scan failed: 0x%x", rc);
            continue;
        }

        int refreshedIndex = -1;
        for (int i = 0; i < sLdnNetworkCount; i++) {
            if (memcmp(selectedBssid, sLdnNetworkInfo[i].common.bssid.addr, sizeof(selectedBssid)) == 0) {
                refreshedIndex = i;
                break;
            }
        }
        if (refreshedIndex < 0) {
            ldn_log("[LDN] selected host disappeared during retry");
            break;
        }
        index = refreshedIndex;
    }
    if (!connected) { return false; }

    sLdnConnected = true;
    // ldnDisconnect returns StationConnected to Station, so keep track of the
    // station being open and close it explicitly during final shutdown.
    sLdnStationOpen = true;
    if (!ldn_udp_open()) {
        ldn_shutdown_impl();
        return false;
    }
    return true;
}

bool ldn_refresh_scan(void) {
    if (sLdnConnected) { return true; }
    if (!sLdnInitialized && !ldn_initialize_impl(false)) { return false; }

    sLdnNetworkCount = 0;
    LdnScanFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.network_id.intent_id.local_communication_id = -1;
    filter.flags = LdnScanFilterFlag_LocalCommunicationId;

    Result rc = ldnScan(0, &filter, sLdnNetworkInfo, 4, &sLdnNetworkCount);
    ldn_log("[LDN] ldnScan: 0x%x networks=%d", rc, sLdnNetworkCount);
    if (R_FAILED(rc)) {
        sLdnNetworkCount = 0;
        return false;
    }
    return true;
}

int ldn_get_network_count(void) {
    return sLdnNetworkCount;
}

const char* ldn_get_network_name(int index) {
    if (index < 0 || index >= sLdnNetworkCount) { return ""; }
    return sLdnNetworkInfo[index].nodes[0].user_name;
}

int ldn_get_network_player_count(int index) {
    if (index < 0 || index >= sLdnNetworkCount) { return 0; }
    return sLdnNetworkInfo[index].node_count;
}

int ldn_get_network_max_players(int index) {
    if (index < 0 || index >= sLdnNetworkCount) { return 0; }
    return sLdnNetworkInfo[index].node_count_max;
}

#endif // __SWITCH__
