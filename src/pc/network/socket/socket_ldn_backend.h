#pragma once

#ifdef __SWITCH__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SM64COOPDX_LDN_MAX_NETWORKS 8

bool ldn_backend_initialize(bool is_server);
void ldn_backend_shutdown(void);
void ldn_backend_update(void);

int ldn_backend_send(uint8_t local_index, void *address, const uint8_t *data, uint16_t data_length);
void *ldn_backend_dup_addr(uint8_t local_index);
bool ldn_backend_match_addr(const void *a, const void *b);
void ldn_backend_save_id(uint8_t local_index);
void ldn_backend_clear_id(uint8_t local_index);

bool ldn_backend_refresh_scan(void);
bool ldn_backend_connect_to_index(int index);
bool ldn_backend_reconnect_last(void);
void ldn_backend_forget_last_network(void);
int ldn_backend_network_count(void);
const char *ldn_backend_network_name(int index);
int ldn_backend_network_player_count(int index);
int ldn_backend_network_max_players(int index);

bool ldn_backend_connected(void);
bool ldn_backend_is_server(void);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */
