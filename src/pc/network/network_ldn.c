#ifdef __SWITCH__

#include "network.h"

// network.c is compiled with network_set_system renamed to
// network_set_system_base for the Switch build. This wrapper keeps current
// upstream networking intact and adds the libnx LDN selector without carrying
// an old copy of network.c forward.
void network_set_system_base(enum NetworkSystemType nsType);

bool ldn_initialize_impl(bool isServer);
void ldn_update_impl(void);
int ldn_send_impl(u8 localIndex, void* addr, u8* data, u16 dataLength);
void ldn_shutdown_impl(void);
void* ldn_dup_addr_impl(u8 localIndex);
bool ldn_match_addr_impl(void* addr1, void* addr2);
void ldn_save_id_impl(u8 localIndex);
void ldn_clear_id_impl(u8 localIndex);

static s64 ldn_get_id(UNUSED u8 localIndex) { return 0; }
static char* ldn_get_id_str(UNUSED u8 localIndex) { return "ldn"; }
static void ldn_get_lobby_id(UNUSED char* destination, UNUSED u32 destLength) {}
static void ldn_get_lobby_secret(UNUSED char* destination, UNUSED u32 destLength) {}
static void ldn_save_id(u8 localIndex, UNUSED s64 networkId) { ldn_save_id_impl(localIndex); }
static void ldn_clear_id(u8 localIndex) { ldn_clear_id_impl(localIndex); }
static void* ldn_dup_addr(u8 localIndex) { return ldn_dup_addr_impl(localIndex); }
static bool ldn_match_addr(void* addr1, void* addr2) { return ldn_match_addr_impl(addr1, addr2); }
static bool ldn_initialize(enum NetworkType networkType, UNUSED bool reconnecting) {
    return ldn_initialize_impl(networkType == NT_SERVER);
}
static void ldn_update(void) { ldn_update_impl(); }
static int ldn_send(u8 localIndex, void* addr, u8* data, u16 dataLength) {
    return ldn_send_impl(localIndex, addr, data, dataLength);
}
static void ldn_shutdown(UNUSED bool reconnecting) { ldn_shutdown_impl(); }

struct NetworkSystem gNetworkSystemLdn = {
    .initialize       = ldn_initialize,
    .get_id           = ldn_get_id,
    .get_id_str       = ldn_get_id_str,
    .save_id          = ldn_save_id,
    .clear_id         = ldn_clear_id,
    .dup_addr         = ldn_dup_addr,
    .match_addr       = ldn_match_addr,
    .update           = ldn_update,
    .send             = ldn_send,
    .get_lobby_id     = ldn_get_lobby_id,
    .get_lobby_secret = ldn_get_lobby_secret,
    .shutdown         = ldn_shutdown,
    .requireServerBroadcast = false,
    .name             = "LDN",
};

void network_set_system(enum NetworkSystemType nsType) {
    if (nsType == NS_LDN) {
        gNetworkSystem = &gNetworkSystemLdn;
        return;
    }
    network_set_system_base(nsType);
}

#endif
