// Host-gcc unit tests for the libnx-free half of the Switch local-wireless
// backend. Run through tools/switch/tests/run_tests.py.

#include <stdio.h>
#include <string.h>

#include "pc/network/socket/socket_ldn_util.h"

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        sChecks++;                                                         \
        if (!(cond)) {                                                     \
            sFailures++;                                                   \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                  \
    } while (0)

#define CHECK_STR(actual, expected)                                        \
    do {                                                                   \
        sChecks++;                                                         \
        if (strcmp((actual), (expected)) != 0) {                           \
            sFailures++;                                                   \
            printf("  FAIL %s:%d: expected \"%s\", got \"%s\"\n",          \
                   __FILE__, __LINE__, (expected), (actual));              \
        }                                                                  \
    } while (0)

static void test_sanitize_user_name(void) {
    char dst[16];

    ldn_util_sanitize_user_name("Mario", dst, sizeof(dst));
    CHECK_STR(dst, "Mario");

    // Control bytes and non-ASCII are dropped, not replaced.
    ldn_util_sanitize_user_name("Ma\trio\n", dst, sizeof(dst));
    CHECK_STR(dst, "Mario");
    ldn_util_sanitize_user_name("Mari\xc3\xb6", dst, sizeof(dst));
    CHECK_STR(dst, "Mari");

    // Space and tilde are the inclusive bounds of the accepted range.
    ldn_util_sanitize_user_name(" ~", dst, sizeof(dst));
    CHECK_STR(dst, " ~");
    ldn_util_sanitize_user_name("\x1f\x7f", dst, sizeof(dst));
    CHECK_STR(dst, "Player");

    // Empty, whitespace-only-after-filtering and NULL all fall back.
    ldn_util_sanitize_user_name("", dst, sizeof(dst));
    CHECK_STR(dst, "Player");
    ldn_util_sanitize_user_name(NULL, dst, sizeof(dst));
    CHECK_STR(dst, "Player");

    // Truncation always leaves room for the terminator.
    ldn_util_sanitize_user_name("0123456789abcdefghij", dst, sizeof(dst));
    CHECK_STR(dst, "0123456789abcde");
    CHECK(strlen(dst) == sizeof(dst) - 1);

    // A one-byte buffer can only hold the terminator; it must not be written
    // past, and the "Player" fallback must not overflow it either.
    char tiny[2] = { 'x', 'x' };
    ldn_util_sanitize_user_name("Mario", tiny, 1);
    CHECK(tiny[0] == '\0');
    CHECK(tiny[1] == 'x');

    // Degenerate arguments are ignored rather than crashing.
    ldn_util_sanitize_user_name("Mario", NULL, sizeof(dst));
    ldn_util_sanitize_user_name("Mario", dst, 0);
}

static void test_addr_conversion(void) {
    // LDN reports 10.13.0.1 host-ordered; sockets want it network-ordered.
    CHECK(ldn_util_addr_to_network_order(0x0A0D0001u) == 0x01000D0Au);
    CHECK(ldn_util_addr_to_network_order(0) == 0);
    CHECK(ldn_util_addr_to_network_order(ldn_util_addr_to_network_order(0x12345678u)) == 0x12345678u);

    CHECK(ldn_util_addr_equal(0x0A0D0001u, 0x0A0D0001u));
    CHECK(!ldn_util_addr_equal(0x0A0D0001u, 0x0A0D0002u));
}

static void test_peer_binding(void) {
    LdnPeerTable peers;
    memset(&peers, 0xAB, sizeof(peers));
    ldn_util_peers_reset(&peers);
    for (int i = 0; i < LDN_UTIL_MAX_PLAYERS; i++) {
        CHECK(peers.addr[i] == 0);
    }

    // An unbound sender is unknown, including the scratch slot's own address.
    peers.addr[0] = 0x0A0D0002u;
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0002u) == LDN_UTIL_UNKNOWN_LOCAL_INDEX);

    // save binds the player slot to whatever last landed in the scratch slot.
    ldn_util_peer_save(&peers, 1);
    CHECK(peers.addr[1] == 0x0A0D0002u);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0002u) == 1);

    peers.addr[0] = 0x0A0D0003u;
    ldn_util_peer_save(&peers, 4);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0003u) == 4);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0002u) == 1);

    // Slot 0 is scratch, never a player: it can be neither saved nor cleared.
    uint32_t scratch = peers.addr[0];
    ldn_util_peer_save(&peers, 0);
    ldn_util_peer_clear(&peers, 0);
    CHECK(peers.addr[0] == scratch);

    // Out-of-range indices are ignored, not clamped into a real slot.
    ldn_util_peer_save(&peers, LDN_UTIL_MAX_PLAYERS);
    ldn_util_peer_save(&peers, LDN_UTIL_UNKNOWN_LOCAL_INDEX);
    ldn_util_peer_clear(&peers, LDN_UTIL_MAX_PLAYERS);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0003u) == 4);

    ldn_util_peer_clear(&peers, 1);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0002u) == LDN_UTIL_UNKNOWN_LOCAL_INDEX);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D0003u) == 4);

    // A zeroed slot must never match a zero-valued sender address.
    CHECK(ldn_util_peer_index_for_addr(&peers, 0) == LDN_UTIL_UNKNOWN_LOCAL_INDEX);

    // The last usable player slot resolves like any other.
    peers.addr[0] = 0x0A0D000Fu;
    ldn_util_peer_save(&peers, LDN_UTIL_MAX_PLAYERS - 1);
    CHECK(ldn_util_peer_index_for_addr(&peers, 0x0A0D000Fu) == LDN_UTIL_MAX_PLAYERS - 1);

    ldn_util_peer_index_for_addr(NULL, 1);
    ldn_util_peers_reset(NULL);
    ldn_util_peer_save(NULL, 1);
    ldn_util_peer_clear(NULL, 1);
}

static void test_reconnect_clears_slots(void) {
    LdnPeerTable peers;
    ldn_util_peers_reset(&peers);
    peers.addr[0] = 0x0A0D0001u;
    peers.addr[1] = 0x0A0D0002u;
    peers.addr[2] = 0x0A0D0003u;

    // A client keeps slot 0: that is the host it is still associated with.
    ldn_util_peers_clear_for_reconnect(&peers, false);
    CHECK(peers.addr[0] == 0x0A0D0001u);
    CHECK(peers.addr[1] == 0);
    CHECK(peers.addr[2] == 0);

    // A host has no such address to keep.
    peers.addr[1] = 0x0A0D0002u;
    ldn_util_peers_clear_for_reconnect(&peers, true);
    CHECK(peers.addr[0] == 0);
    CHECK(peers.addr[1] == 0);

    ldn_util_peers_clear_for_reconnect(NULL, true);
}

static void test_length_validation(void) {
    // Send: the index must be a real slot and the payload must fit.
    CHECK(ldn_util_send_length_valid(0, 0));
    CHECK(ldn_util_send_length_valid(0, LDN_UTIL_PACKET_LENGTH - 1));
    CHECK(!ldn_util_send_length_valid(0, LDN_UTIL_PACKET_LENGTH));
    CHECK(!ldn_util_send_length_valid(0, 65535));
    CHECK(ldn_util_send_length_valid(LDN_UTIL_MAX_PLAYERS - 1, 1));
    CHECK(!ldn_util_send_length_valid(LDN_UTIL_MAX_PLAYERS, 1));
    CHECK(!ldn_util_send_length_valid(LDN_UTIL_UNKNOWN_LOCAL_INDEX, 1));

    // Receive: recvfrom's signed result must be a real, in-bounds datagram.
    CHECK(ldn_util_recv_length_valid(1));
    CHECK(ldn_util_recv_length_valid(LDN_UTIL_PACKET_LENGTH - 1));
    CHECK(!ldn_util_recv_length_valid(LDN_UTIL_PACKET_LENGTH));
    CHECK(!ldn_util_recv_length_valid(0));
    CHECK(!ldn_util_recv_length_valid(-1));
}

int main(void) {
    printf("socket_ldn_util tests\n");
    test_sanitize_user_name();
    test_addr_conversion();
    test_peer_binding();
    test_reconnect_clears_slots();
    test_length_validation();

    if (sFailures > 0) {
        printf("%d/%d checks failed\n", sFailures, sChecks);
        return 1;
    }
    printf("all %d checks passed\n", sChecks);
    return 0;
}
