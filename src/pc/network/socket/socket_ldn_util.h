#ifndef SOCKET_LDN_UTIL_H
#define SOCKET_LDN_UTIL_H

// Pure logic behind the Switch local-wireless backend: no libnx, no project
// headers, no syscalls. socket_ldn.c owns every LDN and socket call and
// delegates the decisions here, so this unit builds with devkitA64 for the
// console and with host gcc for tools/switch/tests/test_socket_ldn_util.c.

#include <stdbool.h>
#include <stdint.h>

#define LDN_UTIL_MAX_PLAYERS 16
#define LDN_UTIL_PACKET_LENGTH 3000
#define LDN_UTIL_UNKNOWN_LOCAL_INDEX ((unsigned char)-1)

// IPv4 addresses, in network byte order. Slot 0 is the scratch slot: on a
// client it holds the host, on a server it holds the most recent sender.
// A zeroed slot means "no peer bound".
typedef struct LdnPeerTable {
    uint32_t addr[LDN_UTIL_MAX_PLAYERS];
} LdnPeerTable;

// Copies the printable ASCII of src into dst, always NUL terminating within
// dstLen. libnx user-name fields are fixed size and reject other bytes, so an
// empty or fully filtered name falls back to "Player".
void ldn_util_sanitize_user_name(const char* src, char* dst, unsigned int dstLen);

// LDN reports IPv4 addresses host-ordered; BSD sockets want network order.
uint32_t ldn_util_addr_to_network_order(uint32_t ldnAddr);

void ldn_util_peers_reset(LdnPeerTable* peers);

// Resolves a datagram sender to a CoopDX player slot, or
// LDN_UTIL_UNKNOWN_LOCAL_INDEX when no slot is bound to it yet.
unsigned char ldn_util_peer_index_for_addr(const LdnPeerTable* peers, uint32_t addr);

// Binds/unbinds a player slot to whatever is currently in the scratch slot.
// Slot 0 is never a player slot, so both calls ignore it.
void ldn_util_peer_save(LdnPeerTable* peers, unsigned char localIndex);
void ldn_util_peer_clear(LdnPeerTable* peers, unsigned char localIndex);

// A CoopDX rehost/rejoin rebuilds player slots while the radio association
// stays up. Drop the stale slot bindings; a client keeps its host address.
void ldn_util_peers_clear_for_reconnect(LdnPeerTable* peers, bool isServer);

bool ldn_util_addr_equal(uint32_t a, uint32_t b);

bool ldn_util_send_length_valid(unsigned char localIndex, unsigned short dataLength);
bool ldn_util_recv_length_valid(long length);

#endif
