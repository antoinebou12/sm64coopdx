#ifndef SOCKET_LDN_H
#define SOCKET_LDN_H

#ifdef __SWITCH__

#include "pc/network/network.h"

extern struct NetworkSystem gNetworkSystemLdn;

bool ldn_connect_to_index(s32 index);
bool ldn_refresh_scan(void);
s32 ldn_get_network_count(void);
const char* ldn_get_network_name(s32 index);
s32 ldn_get_network_player_count(s32 index);
s32 ldn_get_network_max_players(s32 index);

#endif

#endif
