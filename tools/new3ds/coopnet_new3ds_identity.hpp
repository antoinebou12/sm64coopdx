#pragma once

#include <cstddef>
#include <cstdint>

#define COOPNET_NEW3DS_APP_PATH "sdmc:/3ds/sm64coopdx/sm64coopdx.3dsx"

enum CoopNetNew3dsIdentityError {
    COOPNET_NEW3DS_IDENTITY_OK = 0,
    COOPNET_NEW3DS_IDENTITY_OPEN_FAILED,
    COOPNET_NEW3DS_IDENTITY_SIZE_FAILED,
    COOPNET_NEW3DS_IDENTITY_EMPTY,
    COOPNET_NEW3DS_IDENTITY_READ_FAILED,
};

struct CoopNetNew3dsIdentityResult {
    std::uint64_t hash = 0;
    std::uint64_t bytes = 0;
    CoopNetNew3dsIdentityError error = COOPNET_NEW3DS_IDENTITY_OK;
    bool cached = false;
};

CoopNetNew3dsIdentityResult coopnet_new3ds_hash_file(const char* path);
CoopNetNew3dsIdentityResult coopnet_new3ds_identity(const char* path);
void coopnet_new3ds_identity_reset_cache(void);
