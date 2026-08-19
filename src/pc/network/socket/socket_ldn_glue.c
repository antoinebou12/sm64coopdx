#ifdef __SWITCH__

#include "socket_ldn.h"
#include "pc/network/network.h"

// Implemented by socket_ldn.c. Keep this boundary free of libnx integer
// typedefs so <switch.h> never collides with the project's PR/ultratypes.h.
extern bool ldn_initialize_impl(bool isServer);
extern void ldn_update_impl(void);
extern int ldn_send_impl(unsigned char localIndex, void* addr, unsigned char* data, unsigned short dataLength);
extern void ldn_shutdown_impl(void);
extern void* ldn_dup_addr_impl(unsigned char localIndex);
extern bool ldn_match_addr_impl(void* addr1, void* addr2);
extern void ldn_save_id_impl(unsigned char localIndex);
extern void ldn_clear_id_impl(unsigned char localIndex);

static s64 ns_ldn_get_id(UNUSED u8 localIndex) {
    return 0;
}

static char* ns_ldn_get_id_str(UNUSED u8 localIndex) {
    return "ldn";
}

static void ns_ldn_save_id(u8 localIndex, UNUSED s64 networkId) {
    ldn_save_id_impl(localIndex);
}

static void ns_ldn_clear_id(u8 localIndex) {
    ldn_clear_id_impl(localIndex);
}

static void* ns_ldn_dup_addr(u8 localIndex) {
    return ldn_dup_addr_impl(localIndex);
}

static bool ns_ldn_match_addr(void* addr1, void* addr2) {
    return ldn_match_addr_impl(addr1, addr2);
}

static bool ns_ldn_initialize(enum NetworkType networkType, UNUSED bool reconnecting) {
    return ldn_initialize_impl(networkType == NT_SERVER);
}

static void ns_ldn_update(void) {
    ldn_update_impl();
}

static int ns_ldn_send(u8 localIndex, void* addr, u8* data, u16 dataLength) {
    return ldn_send_impl(localIndex, addr, data, dataLength);
}

static void ns_ldn_get_lobby_id(char* destination, u32 destLength) {
    if (destination != NULL && destLength > 0) {
        destination[0] = '\0';
    }
}

static void ns_ldn_get_lobby_secret(char* destination, u32 destLength) {
    if (destination != NULL && destLength > 0) {
        destination[0] = '\0';
    }
}

static void ns_ldn_shutdown(UNUSED bool reconnecting) {
    ldn_shutdown_impl();
}

struct NetworkSystem gNetworkSystemLdn = {
    .initialize       = ns_ldn_initialize,
    .get_id           = ns_ldn_get_id,
    .get_id_str       = ns_ldn_get_id_str,
    .save_id          = ns_ldn_save_id,
    .clear_id         = ns_ldn_clear_id,
    .dup_addr         = ns_ldn_dup_addr,
    .match_addr       = ns_ldn_match_addr,
    .update           = ns_ldn_update,
    .send             = ns_ldn_send,
    .get_lobby_id     = ns_ldn_get_lobby_id,
    .get_lobby_secret = ns_ldn_get_lobby_secret,
    // Match the normal UDP backend: clients send broadcast traffic through
    // the host, which already knows every LDN peer address.
    .requireServerBroadcast = true,
    .name             = "Local Wireless",
    .shutdown         = ns_ldn_shutdown,
};

#endif
