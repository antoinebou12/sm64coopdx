#include "socket_ldn_util.h"

#include <stdio.h>
#include <string.h>

void ldn_util_sanitize_user_name(const char* src, char* dst, unsigned int dstLen) {
    if (dst == NULL || dstLen == 0) { return; }

    unsigned int o = 0;
    if (src != NULL) {
        for (unsigned int i = 0; src[i] != '\0' && o + 1 < dstLen; i++) {
            unsigned char c = (unsigned char)src[i];
            if (c >= 0x20 && c <= 0x7E) { dst[o++] = (char)c; }
        }
    }
    dst[o] = '\0';
    if (o == 0) { snprintf(dst, dstLen, "Player"); }
}

uint32_t ldn_util_addr_to_network_order(uint32_t ldnAddr) {
    return __builtin_bswap32(ldnAddr);
}

void ldn_util_peers_reset(LdnPeerTable* peers) {
    if (peers == NULL) { return; }
    memset(peers->addr, 0, sizeof(peers->addr));
}

unsigned char ldn_util_peer_index_for_addr(const LdnPeerTable* peers, uint32_t addr) {
    if (peers == NULL) { return LDN_UTIL_UNKNOWN_LOCAL_INDEX; }
    for (int i = 1; i < LDN_UTIL_MAX_PLAYERS; i++) {
        if (peers->addr[i] != 0 && peers->addr[i] == addr) {
            return (unsigned char)i;
        }
    }
    return LDN_UTIL_UNKNOWN_LOCAL_INDEX;
}

void ldn_util_peer_save(LdnPeerTable* peers, unsigned char localIndex) {
    if (peers == NULL) { return; }
    if (localIndex == 0 || localIndex >= LDN_UTIL_MAX_PLAYERS) { return; }
    peers->addr[localIndex] = peers->addr[0];
}

void ldn_util_peer_clear(LdnPeerTable* peers, unsigned char localIndex) {
    if (peers == NULL) { return; }
    if (localIndex == 0 || localIndex >= LDN_UTIL_MAX_PLAYERS) { return; }
    peers->addr[localIndex] = 0;
}

void ldn_util_peers_clear_for_reconnect(LdnPeerTable* peers, bool isServer) {
    if (peers == NULL) { return; }
    for (int i = 1; i < LDN_UTIL_MAX_PLAYERS; i++) {
        peers->addr[i] = 0;
    }
    if (isServer) { peers->addr[0] = 0; }
}

bool ldn_util_addr_equal(uint32_t a, uint32_t b) {
    return a == b;
}

bool ldn_util_send_length_valid(unsigned char localIndex, unsigned short dataLength) {
    return localIndex < LDN_UTIL_MAX_PLAYERS && dataLength < LDN_UTIL_PACKET_LENGTH;
}

bool ldn_util_recv_length_valid(long length) {
    return length > 0 && length < LDN_UTIL_PACKET_LENGTH;
}
