#pragma once

#ifdef __SWITCH__

#include "../network.h"

extern struct NetworkSystem gNetworkSystemLdn;

/* Select LDN as the active CoopDX transport. */
void network_ldn_select(void);

/* Local-wireless lobby discovery helpers for the Switch UI. */
bool network_ldn_refresh_scan(void);
bool network_ldn_connect_to_index(int index);
int network_ldn_network_count(void);
const char *network_ldn_network_name(int index);
int network_ldn_network_player_count(int index);
int network_ldn_network_max_players(int index);

#endif /* __SWITCH__ */
