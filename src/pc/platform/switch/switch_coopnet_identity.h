#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SWITCH_COOPNET_NRO_PATH "sdmc:/switch/sm64coopdx/sm64coopdx.nro"
#define SWITCH_COOPNET_HASH_CHUNK_SIZE (16u * 1024u)

#ifdef __cplusplus
extern "C" {
#endif

size_t switch_coopnet_identity_hash_path(const char *path, uint64_t *bytes_processed);
size_t switch_coopnet_identity_get(uint64_t *bytes_processed, bool *cache_hit);
void switch_coopnet_identity_reset_cache_for_tests(void);

#ifdef __cplusplus
}
#endif
