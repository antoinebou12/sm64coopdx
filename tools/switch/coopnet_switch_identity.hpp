#pragma once

#include <cstddef>
#include <cstdint>

#define COOPNET_SWITCH_NRO_PATH "sdmc:/switch/sm64coopdx/sm64coopdx.nro"

enum CoopNetSwitchIdentityError {
    COOPNET_SWITCH_IDENTITY_OK = 0,
    COOPNET_SWITCH_IDENTITY_OPEN_FAILED,
    COOPNET_SWITCH_IDENTITY_SIZE_FAILED,
    COOPNET_SWITCH_IDENTITY_EMPTY,
    COOPNET_SWITCH_IDENTITY_READ_FAILED,
};

struct CoopNetSwitchIdentityResult {
    std::uint64_t hash = 0;
    std::uint64_t bytes = 0;
    CoopNetSwitchIdentityError error = COOPNET_SWITCH_IDENTITY_OK;
    bool cached = false;
};

// Matches the 64-bit libstdc++ std::hash<std::string> byte hash used by
// CoopNet, but processes the file in bounded chunks instead of materializing
// the whole NRO in memory.
CoopNetSwitchIdentityResult coopnet_switch_hash_file(const char* path);

// Cache only a successful, non-zero identity. Reconnects therefore avoid
// rereading the NRO while a failed lookup can recover after installation is
// corrected.
CoopNetSwitchIdentityResult coopnet_switch_identity(const char* path);

// Test-only cache control; not used by the game.
void coopnet_switch_identity_reset_cache(void);
