#include "switch_coopnet_identity.h"
#include "switch_coopnet_log.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define FNV1A64_PRIME  UINT64_C(1099511628211)

static bool sIdentityCached = false;
static size_t sIdentityFingerprint = 0;
static uint64_t sIdentityBytes = 0;

static uint64_t fnv1a64_update(uint64_t hash, const unsigned char *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= FNV1A64_PRIME;
    }
    return hash;
}

size_t switch_coopnet_identity_hash_path(const char *path, uint64_t *bytes_processed) {
    if (bytes_processed != NULL) { *bytes_processed = 0; }
    if (path == NULL || path[0] == '\0') { return 0; }

    FILE *file = fopen(path, "rb");
    if (file == NULL) { return 0; }

    unsigned char chunk[SWITCH_COOPNET_HASH_CHUNK_SIZE];
    uint64_t hash = FNV1A64_OFFSET;
    uint64_t total = 0;
    bool failed = false;

    for (;;) {
        const size_t count = fread(chunk, 1, sizeof(chunk), file);
        if (count > 0) {
            hash = fnv1a64_update(hash, chunk, count);
            total += (uint64_t)count;
        }
        if (count < sizeof(chunk)) {
            if (ferror(file)) { failed = true; }
            break;
        }
    }
    fclose(file);

    if (bytes_processed != NULL) { *bytes_processed = total; }
    if (failed || total == 0 || hash == 0 || (size_t)hash == 0) { return 0; }
    return (size_t)hash;
}

size_t switch_coopnet_identity_get(uint64_t *bytes_processed, bool *cache_hit) {
    if (sIdentityCached) {
        if (bytes_processed != NULL) { *bytes_processed = sIdentityBytes; }
        if (cache_hit != NULL) { *cache_hit = true; }
        switch_coopnet_log_printf(
            "coopnet identity cache path=%s bytes=%" PRIu64 " fingerprint=%016" PRIx64,
            SWITCH_COOPNET_NRO_PATH,
            sIdentityBytes,
            (uint64_t)sIdentityFingerprint);
        return sIdentityFingerprint;
    }

    if (cache_hit != NULL) { *cache_hit = false; }
    uint64_t bytes = 0;
    errno = 0;
    const size_t fingerprint = switch_coopnet_identity_hash_path(SWITCH_COOPNET_NRO_PATH, &bytes);
    const int saved_errno = errno;
    if (bytes_processed != NULL) { *bytes_processed = bytes; }

    if (fingerprint == 0) {
        switch_coopnet_log_printf(
            "coopnet identity failure path=%s bytes=%" PRIu64 " errno=%d (%s)",
            SWITCH_COOPNET_NRO_PATH,
            bytes,
            saved_errno,
            saved_errno ? strerror(saved_errno) : "invalid or empty NRO");
        return 0;
    }

    sIdentityCached = true;
    sIdentityFingerprint = fingerprint;
    sIdentityBytes = bytes;
    switch_coopnet_log_printf(
        "coopnet identity ready path=%s bytes=%" PRIu64 " fingerprint=%016" PRIx64,
        SWITCH_COOPNET_NRO_PATH,
        bytes,
        (uint64_t)fingerprint);
    return fingerprint;
}

void switch_coopnet_identity_reset_cache_for_tests(void) {
    sIdentityCached = false;
    sIdentityFingerprint = 0;
    sIdentityBytes = 0;
}
