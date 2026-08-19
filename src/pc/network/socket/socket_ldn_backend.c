#ifdef __SWITCH__

/*
 * Keep libnx out of the regular CoopDX networking translation units.
 * libnx and the N64 compatibility headers intentionally use different C
 * typedefs for a few 64-bit types, so this file exposes only fixed-width C
 * types and opaque addresses to the rest of the project.
 */
#include "socket_ldn_backend.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LDN_MAX_PLAYERS 16
#define LDN_UDP_PORT 7777
#define LDN_PACKET_LENGTH 3000
#define LDN_UNKNOWN_LOCAL_INDEX ((uint8_t)0xFF)

/*
 * -1 tells libnx/Horizon to resolve the LocalCommunicationId from the NACP.
 * Homebrew NACPs that do not define one resolve to 0. A hard-coded non--1
 * value is invalid unless that exact value is also declared in the NACP.
 */
#define LDN_LOCAL_COMMUNICATION_ID ((int64_t)-1)

extern void network_ldn_receive_bridge(uint8_t local_index, void *addr, uint8_t *data, uint16_t data_length);
extern const char *network_ldn_player_name_bridge(void);

static bool sInitialized = false;
static bool sAccessPointOpen = false;
static bool sStationOpen = false;
static bool sConnected = false;
static bool sIsServer = false;
static bool sSocketServiceUp = false;
static int sUdpSocket = -1;

static LdnNetworkInfo sNetworks[SM64COOPDX_LDN_MAX_NETWORKS];
static int sNetworkCount = 0;
static LdnMacAddress sLastNetworkBssid;
static bool sLastNetworkValid = false;
static struct in_addr sPeerAddress[LDN_MAX_PLAYERS];
static struct in_addr sOwnAddress;
static uint64_t sLastRefreshTick = 0;

static struct in_addr ldn_to_in_addr(LdnIpv4Address address) {
    struct in_addr result;
    result.s_addr = __builtin_bswap32(address.addr);
    return result;
}

static void sanitize_user_name(char *dst, size_t dst_len) {
    if (dst == NULL || dst_len == 0) return;

    const char *src = network_ldn_player_name_bridge();
    size_t out = 0;
    if (src != NULL) {
        for (size_t i = 0; src[i] != '\0' && out + 1 < dst_len; i++) {
            const unsigned char c = (unsigned char)src[i];
            if (c >= 0x20 && c <= 0x7E) dst[out++] = (char)c;
        }
    }

    if (out == 0) {
        snprintf(dst, dst_len, "Player");
    } else {
        dst[out] = '\0';
    }
}

static void make_security_config(LdnSecurityConfig *security) {
    memset(security, 0, sizeof(*security));
    security->security_mode = LdnSecurityMode_Product;
    security->passphrase_size = 0x10;
    memset(security->passphrase, 0x42, security->passphrase_size);
}

static void make_user_config(LdnUserConfig *user) {
    memset(user, 0, sizeof(*user));
    sanitize_user_name(user->user_name, sizeof(user->user_name));
}

static void clear_peer_addresses(void) {
    memset(sPeerAddress, 0, sizeof(sPeerAddress));
}

static int find_network_by_bssid(const LdnMacAddress *bssid) {
    if (bssid == NULL) return -1;
    for (int i = 0; i < sNetworkCount; i++) {
        if (memcmp(sNetworks[i].common.bssid.addr, bssid->addr, sizeof(bssid->addr)) == 0) {
            return i;
        }
    }
    return -1;
}

static bool refresh_nodes(void) {
    if (!sConnected) return false;

    LdnNetworkInfo info;
    const Result rc = ldnGetNetworkInfo(&info);
    if (R_FAILED(rc)) return false;

    /* node 0 is the access point. Clients always send server-bound packets there. */
    if (!sIsServer && info.node_count > 0 && info.nodes[0].is_connected) {
        sPeerAddress[0] = ldn_to_in_addr(info.nodes[0].ip_addr);
    }
    return true;
}

static void udp_close(void) {
    if (sUdpSocket >= 0) {
        close(sUdpSocket);
        sUdpSocket = -1;
    }
    clear_peer_addresses();
}

static bool udp_open(void) {
    if (!sSocketServiceUp) {
        const Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) return false;
        sSocketServiceUp = true;
    }

    LdnIpv4Address ldn_address;
    LdnSubnetMask subnet_mask;
    if (R_FAILED(ldnGetIpv4Address(&ldn_address, &subnet_mask))) return false;
    (void)subnet_mask;
    sOwnAddress = ldn_to_in_addr(ldn_address);

    sUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sUdpSocket < 0) return false;

    const int flags = fcntl(sUdpSocket, F_GETFL, 0);
    if (flags >= 0) fcntl(sUdpSocket, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bind_address;
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(LDN_UDP_PORT);
    bind_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(sUdpSocket, (struct sockaddr *)&bind_address, sizeof(bind_address)) < 0) {
        udp_close();
        return false;
    }

    clear_peer_addresses();
    sLastRefreshTick = svcGetSystemTick();
    refresh_nodes();
    return true;
}

static bool connection_state_is_valid(void) {
    if (!sInitialized || !sConnected) return false;

    LdnState state = LdnState_None;
    if (R_FAILED(ldnGetState(&state))) return false;

    const LdnState expected = sIsServer
        ? LdnState_AccessPointCreated
        : LdnState_StationConnected;
    if (state == expected) return true;

    /*
     * HOME/sleep, signal loss and system disconnects can invalidate LDN while
     * the UDP socket remains open. Close that stale path immediately so the
     * higher-level CoopDX reconnect logic sees send failures instead of a
     * silent black hole.
     */
    udp_close();
    sConnected = false;
    sStationOpen = (!sIsServer && state == LdnState_Station);
    sAccessPointOpen = (sIsServer && state == LdnState_AccessPoint);
    return false;
}

static bool initialize_ldn_service(void) {
    if (sInitialized) return true;

    Result rc = 0;
    bool initialized = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        rc = ldnInitialize(LdnServiceType_User);
        if (R_SUCCEEDED(rc)) {
            initialized = true;
            break;
        }
        svcSleepThread(250000000ULL);
    }
    if (!initialized) return false;

    sInitialized = true;

    /* HighSpeed is supported by ldn:u on newer Horizon. Failure is harmless. */
    (void)ldnSetOperationMode(LdnOperationMode_HighSpeed);
    return true;
}

static bool ensure_station_open(void) {
    if (!initialize_ldn_service()) return false;

    LdnState state = LdnState_None;
    if (R_FAILED(ldnGetState(&state))) return false;
    if (state == LdnState_Initialized) {
        if (R_FAILED(ldnOpenStation())) return false;
        sStationOpen = true;
        return true;
    }
    if (state == LdnState_Station) {
        sStationOpen = true;
        return true;
    }
    if (state == LdnState_StationConnected) {
        return connection_state_is_valid();
    }
    return false;
}

bool ldn_backend_initialize(bool is_server) {
    if (sInitialized && sConnected && sIsServer == is_server && connection_state_is_valid()) {
        return true;
    }

    if (sInitialized && sIsServer != is_server) {
        ldn_backend_shutdown();
    }
    sIsServer = is_server;

    if (!initialize_ldn_service()) return false;

    if (is_server) {
        if (!sAccessPointOpen) {
            LdnState state = LdnState_None;
            if (R_SUCCEEDED(ldnGetState(&state)) && state == LdnState_AccessPoint) {
                sAccessPointOpen = true;
            } else {
                if (R_FAILED(ldnOpenAccessPoint())) return false;
                sAccessPointOpen = true;
            }
        }

        LdnSecurityConfig security;
        LdnUserConfig user;
        LdnNetworkConfig config;
        make_security_config(&security);
        make_user_config(&user);
        memset(&config, 0, sizeof(config));
        config.intent_id.local_communication_id = LDN_LOCAL_COMMUNICATION_ID;
        config.channel = 0;
        config.node_count_max = 8;
        config.local_communication_version = 0;

        if (R_FAILED(ldnCreateNetwork(&security, &user, &config))) {
            ldnCloseAccessPoint();
            sAccessPointOpen = false;
            return false;
        }

        sConnected = true;
        (void)ldnSetStationAcceptPolicy(LdnAcceptPolicy_AlwaysAccept);
        if (!udp_open()) {
            ldn_backend_shutdown();
            return false;
        }
    } else if (!sStationOpen && !sConnected) {
        if (!ensure_station_open()) return false;
    }

    return true;
}

void ldn_backend_update(void) {
    if (!sInitialized || !sConnected || sUdpSocket < 0) return;
    if (!connection_state_is_valid()) return;

    const uint64_t now = svcGetSystemTick();
    const uint64_t frequency = armGetSystemTickFreq();
    if (now - sLastRefreshTick >= frequency * 2) {
        refresh_nodes();
        sLastRefreshTick = now;
    }

    uint8_t buffer[LDN_PACKET_LENGTH];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const ssize_t length = recvfrom(
            sUdpSocket, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&from, &from_len
        );
        if (length <= 0) {
            if (length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                udp_close();
                sConnected = false;
            }
            break;
        }

        /* Slot zero is the reply-to-sender scratch address, matching socket.c. */
        sPeerAddress[0] = from.sin_addr;
        uint8_t local_index = LDN_UNKNOWN_LOCAL_INDEX;
        for (uint8_t i = 1; i < LDN_MAX_PLAYERS; i++) {
            if (sPeerAddress[i].s_addr != 0 &&
                sPeerAddress[i].s_addr == from.sin_addr.s_addr) {
                local_index = i;
                break;
            }
        }

        if (length <= UINT16_MAX) {
            network_ldn_receive_bridge(local_index, &sPeerAddress[0], buffer, (uint16_t)length);
        }
    }
}

int ldn_backend_send(uint8_t local_index, void *address, const uint8_t *data, uint16_t data_length) {
    if (!sConnected || sUdpSocket < 0 || data == NULL || local_index >= LDN_MAX_PLAYERS) return -1;
    if (!connection_state_is_valid()) return -1;

    struct in_addr destination = sPeerAddress[local_index];
    if (local_index == 0 && address != NULL) {
        destination = *(const struct in_addr *)address;
    }
    if (destination.s_addr == 0) return -1;

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(LDN_UDP_PORT);
    to.sin_addr = destination;

    const ssize_t sent = sendto(
        sUdpSocket, data, data_length, 0,
        (struct sockaddr *)&to, sizeof(to)
    );
    if (sent != (ssize_t)data_length) {
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            udp_close();
            sConnected = false;
        }
        return -1;
    }
    return (int)sent;
}

void *ldn_backend_dup_addr(uint8_t local_index) {
    if (local_index >= LDN_MAX_PLAYERS) local_index = 0;
    struct in_addr *copy = malloc(sizeof(*copy));
    if (copy != NULL) *copy = sPeerAddress[local_index];
    return copy;
}

bool ldn_backend_match_addr(const void *a, const void *b) {
    if (a == NULL || b == NULL) return false;
    return ((const struct in_addr *)a)->s_addr == ((const struct in_addr *)b)->s_addr;
}

void ldn_backend_save_id(uint8_t local_index) {
    if (local_index == 0 || local_index >= LDN_MAX_PLAYERS) return;
    sPeerAddress[local_index] = sPeerAddress[0];
}

void ldn_backend_clear_id(uint8_t local_index) {
    if (local_index == 0 || local_index >= LDN_MAX_PLAYERS) return;
    sPeerAddress[local_index].s_addr = 0;
}

bool ldn_backend_refresh_scan(void) {
    if (sConnected && connection_state_is_valid()) return true;
    if (!ensure_station_open()) return false;

    LdnScanFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.network_id.intent_id.local_communication_id = LDN_LOCAL_COMMUNICATION_ID;
    filter.flags = LdnScanFilterFlag_LocalCommunicationId;

    sNetworkCount = 0;
    const Result rc = ldnScan(
        0, &filter, sNetworks, SM64COOPDX_LDN_MAX_NETWORKS, &sNetworkCount
    );
    if (R_FAILED(rc)) {
        sNetworkCount = 0;
        return false;
    }
    return true;
}

static bool connect_to_bssid(const LdnMacAddress *bssid) {
    if (bssid == NULL) return false;
    if (!ensure_station_open()) return false;

    LdnSecurityConfig security;
    LdnUserConfig user;
    make_security_config(&security);
    make_user_config(&user);

    for (int attempt = 0; attempt < 10; attempt++) {
        if (!ldn_backend_refresh_scan()) return false;
        const int index = find_network_by_bssid(bssid);
        if (index >= 0) {
            const Result rc = ldnConnect(&security, &user, 0, 0, &sNetworks[index]);
            if (R_SUCCEEDED(rc)) {
                sConnected = true;
                sStationOpen = false;
                sLastNetworkBssid = sNetworks[index].common.bssid;
                sLastNetworkValid = true;
                if (!udp_open()) {
                    ldn_backend_shutdown();
                    return false;
                }
                return true;
            }
        }

        /* Short first retry, then bounded exponential backoff. */
        uint64_t delay_ns = 150000000ULL << (attempt < 3 ? attempt : 3);
        if (delay_ns > 1200000000ULL) delay_ns = 1200000000ULL;
        svcSleepThread(delay_ns);
    }
    return false;
}

bool ldn_backend_connect_to_index(int index) {
    if (index < 0 || index >= sNetworkCount) return false;
    const LdnMacAddress target = sNetworks[index].common.bssid;
    return connect_to_bssid(&target);
}

bool ldn_backend_reconnect_last(void) {
    if (!sLastNetworkValid) return false;
    return connect_to_bssid(&sLastNetworkBssid);
}

void ldn_backend_forget_last_network(void) {
    memset(&sLastNetworkBssid, 0, sizeof(sLastNetworkBssid));
    sLastNetworkValid = false;
}

int ldn_backend_network_count(void) {
    return sNetworkCount;
}

const char *ldn_backend_network_name(int index) {
    if (index < 0 || index >= sNetworkCount || sNetworks[index].node_count <= 0) return "";
    return sNetworks[index].nodes[0].user_name;
}

int ldn_backend_network_player_count(int index) {
    if (index < 0 || index >= sNetworkCount) return 0;
    return sNetworks[index].node_count;
}

int ldn_backend_network_max_players(int index) {
    if (index < 0 || index >= sNetworkCount) return 0;
    return sNetworks[index].node_count_max;
}

bool ldn_backend_connected(void) {
    return sConnected;
}

bool ldn_backend_is_server(void) {
    return sIsServer;
}

void ldn_backend_shutdown(void) {
    udp_close();

    if (sInitialized) {
        LdnState state = LdnState_None;
        (void)ldnGetState(&state);

        if (state == LdnState_AccessPointCreated) {
            (void)ldnDestroyNetwork();
            state = LdnState_AccessPoint;
        } else if (state == LdnState_StationConnected) {
            (void)ldnDisconnect();
            state = LdnState_Station;
        }

        if (state == LdnState_Station || sStationOpen) {
            (void)ldnCloseStation();
        }
        if (state == LdnState_AccessPoint || sAccessPointOpen) {
            (void)ldnCloseAccessPoint();
        }

        ldnExit();
        sInitialized = false;
    }

    if (sSocketServiceUp) {
        socketExit();
        sSocketServiceUp = false;
    }

    sConnected = false;
    sStationOpen = false;
    sAccessPointOpen = false;
    sNetworkCount = 0;
    sIsServer = false;
    memset(&sOwnAddress, 0, sizeof(sOwnAddress));
}

#endif /* __SWITCH__ */
