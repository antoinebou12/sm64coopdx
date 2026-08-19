#ifdef __SWITCH__

#include "socket_ldn.h"
#include "socket_ldn_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/configfile.h"
#include "pc/debuglog.h"

static char sLdnIdString[32] = "ldn";

/*
 * These two bridge functions are deliberately the only symbols called by the
 * libnx translation unit. That keeps <switch.h> and CoopDX's N64 compatibility
 * typedefs in separate translation units.
 */
void network_ldn_receive_bridge(uint8_t local_index, void *addr, uint8_t *data, uint16_t data_length) {
    network_receive((u8)local_index, addr, (u8 *)data, (u16)data_length);
}

const char *network_ldn_player_name_bridge(void) {
    return configPlayerName;
}

static bool ldn_initialize(enum NetworkType type, bool reconnecting) {
    (void)reconnecting;
    if (type == NT_NONE) return true;
    return ldn_backend_initialize(type == NT_SERVER);
}

static s64 ldn_get_id(u8 local_index) {
    /*
     * LDN peers are addressed by their per-session IPv4 address. CoopDX only
     * needs a stable non-zero opaque identifier here, while address matching
     * and routing are handled by dup_addr/match_addr/save_id below.
     */
    return (s64)local_index + 1;
}

static char *ldn_get_id_str(u8 local_index) {
    snprintf(sLdnIdString, sizeof(sLdnIdString), "ldn-%u", local_index);
    return sLdnIdString;
}

static void ldn_save_id(u8 local_index, UNUSED s64 network_id) {
    ldn_backend_save_id((uint8_t)local_index);
}

static void ldn_clear_id(u8 local_index) {
    ldn_backend_clear_id((uint8_t)local_index);
}

static void *ldn_dup_addr(u8 local_index) {
    return ldn_backend_dup_addr((uint8_t)local_index);
}

static bool ldn_match_addr(void *addr1, void *addr2) {
    return ldn_backend_match_addr(addr1, addr2);
}

static void ldn_update(void) {
    ldn_backend_update();
}

static int ldn_send(u8 local_index, void *addr, u8 *data, u16 data_length) {
    return ldn_backend_send((uint8_t)local_index, addr, (const uint8_t *)data, (uint16_t)data_length);
}

static void ldn_get_lobby_id(char *destination, u32 destination_length) {
    if (destination == NULL || destination_length == 0) return;
    snprintf(destination, destination_length, "local-wireless");
}

static void ldn_get_lobby_secret(char *destination, u32 destination_length) {
    if (destination == NULL || destination_length == 0) return;
    destination[0] = '\0';
}

static void ldn_shutdown(UNUSED bool reconnecting) {
    ldn_backend_shutdown();
}

struct NetworkSystem gNetworkSystemLdn = {
    .initialize = ldn_initialize,
    .get_id = ldn_get_id,
    .get_id_str = ldn_get_id_str,
    .save_id = ldn_save_id,
    .clear_id = ldn_clear_id,
    .dup_addr = ldn_dup_addr,
    .match_addr = ldn_match_addr,
    .update = ldn_update,
    .send = ldn_send,
    .get_lobby_id = ldn_get_lobby_id,
    .get_lobby_secret = ldn_get_lobby_secret,
    .shutdown = ldn_shutdown,
    .requireServerBroadcast = false,
    .name = "Local Wireless",
};

void network_ldn_select(void) {
    network_forget_all_reliable();
    gNetworkSystem = &gNetworkSystemLdn;
    LOG_INFO("selected Switch local wireless transport");
}

bool network_ldn_refresh_scan(void) {
    return ldn_backend_refresh_scan();
}

bool network_ldn_connect_to_index(int index) {
    return ldn_backend_connect_to_index(index);
}

void network_ldn_cancel_scan(void) {
    /* Keep an established game session alive when its lobby panel disappears. */
    if (gNetworkType == NT_NONE) {
        ldn_backend_shutdown();
    }
}

int network_ldn_network_count(void) {
    return ldn_backend_network_count();
}

const char *network_ldn_network_name(int index) {
    return ldn_backend_network_name(index);
}

int network_ldn_network_player_count(int index) {
    return ldn_backend_network_player_count(index);
}

int network_ldn_network_max_players(int index) {
    return ldn_backend_network_max_players(index);
}

#endif /* __SWITCH__ */
