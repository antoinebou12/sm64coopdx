#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

#include "pc/debuglog.h"
#include "pc/network/network.h"

#ifdef __SWITCH__

#define SWITCH_PACKET_HASH_LENGTH ((u16)sizeof(u32))
#define SWITCH_PACKET_DATA_LENGTH ((u16)(PACKET_LENGTH - SWITCH_PACKET_HASH_LENGTH))
#define SWITCH_PACKET_BASE_HEADER_LENGTH ((u16)(sizeof(u8) + sizeof(u16) + sizeof(u8) + sizeof(u8)))

static Bytef* sSwitchCompBuffer = NULL;
static uLongf sSwitchCompBufferCapacity = 0;

static bool switch_packet_payload_valid(const struct Packet* p, const char* caller) {
    if (p == NULL) {
        LOG_ERROR("%s: null packet", caller);
        return false;
    }
    if (p->dataLength > SWITCH_PACKET_DATA_LENGTH) {
        LOG_ERROR("%s: refusing oversized packet type=%u dataLength=%u capacity=%u",
                  caller, p->packetType, p->dataLength, SWITCH_PACKET_DATA_LENGTH);
        return false;
    }
    if (p->error || p->writeError) {
        LOG_ERROR("%s: refusing packet with write/error state type=%u", caller, p->packetType);
        return false;
    }
    return true;
}

void __real_network_send(struct Packet* p);
void __wrap_network_send(struct Packet* p) {
    if (!switch_packet_payload_valid(p, "network_send")) {
        return;
    }
    __real_network_send(p);
}

void __real_network_send_to(u8 localIndex, struct Packet* p);
void __wrap_network_send_to(u8 localIndex, struct Packet* p) {
    if (!switch_packet_payload_valid(p, "network_send_to")) {
        return;
    }

    if (localIndex != PACKET_DESTINATION_SERVER &&
        localIndex != UNKNOWN_LOCAL_INDEX &&
        localIndex >= MAX_PLAYERS) {
        LOG_ERROR("network_send_to: refusing invalid localIndex=%u", localIndex);
        return;
    }

    __real_network_send_to(localIndex, p);
}

void __real_network_receive(u8 localIndex, void* addr, u8* data, u16 dataLength);
void __wrap_network_receive(u8 localIndex, void* addr, u8* data, u16 dataLength) {
    if (data == NULL || dataLength == 0) {
        LOG_ERROR("network_receive: refusing empty packet");
        return;
    }

    if (localIndex != UNKNOWN_LOCAL_INDEX && localIndex >= MAX_PLAYERS) {
        LOG_ERROR("network_receive: refusing invalid localIndex=%u", localIndex);
        return;
    }

    __real_network_receive(localIndex, addr, data, dataLength);
}

void __wrap_packet_compress(struct Packet* p, u8** compBuffer, u32* compSize) {
    if (compBuffer == NULL || compSize == NULL) {
        return;
    }

    *compBuffer = NULL;
    *compSize = 0;

    if (!switch_packet_payload_valid(p, "packet_compress")) {
        return;
    }

    const uLong sourceSize = (uLong)p->dataLength + SWITCH_PACKET_HASH_LENGTH;
    const uLongf requiredCapacity = compressBound(sourceSize);
    if (requiredCapacity == 0 || requiredCapacity > UINT32_MAX) {
        LOG_ERROR("packet_compress: invalid compressBound=%lu", (unsigned long)requiredCapacity);
        return;
    }

    if (requiredCapacity > sSwitchCompBufferCapacity || sSwitchCompBuffer == NULL) {
        Bytef* resized = (Bytef*)realloc(sSwitchCompBuffer, requiredCapacity);
        if (resized == NULL) {
            LOG_ERROR("packet_compress: allocation failed for %lu bytes", (unsigned long)requiredCapacity);
            return;
        }
        sSwitchCompBuffer = resized;
        sSwitchCompBufferCapacity = requiredCapacity;
    }

    uLongf compressedLen = sSwitchCompBufferCapacity;
    const int rc = compress2(sSwitchCompBuffer, &compressedLen,
                             (const Bytef*)p->buffer, sourceSize, Z_BEST_COMPRESSION);
    if (rc != Z_OK || compressedLen == 0 || compressedLen > UINT32_MAX) {
        LOG_ERROR("packet_compress: zlib failed rc=%d len=%lu", rc, (unsigned long)compressedLen);
        return;
    }

    *compBuffer = (u8*)sSwitchCompBuffer;
    *compSize = (u32)compressedLen;
}

bool __wrap_packet_decompress(struct Packet* p, u8* compBuffer, u32 compSize) {
    if (p == NULL || compBuffer == NULL || compSize == 0) {
        return false;
    }

    uLongf decodedLen = PACKET_LENGTH;
    const int rc = uncompress((Bytef*)p->buffer, &decodedLen,
                              (const Bytef*)compBuffer, (uLong)compSize);
    if (rc != Z_OK) {
        LOG_ERROR("packet_decompress: zlib failed rc=%d", rc);
        return false;
    }

    const uLong minDecoded = (uLong)SWITCH_PACKET_BASE_HEADER_LENGTH + SWITCH_PACKET_HASH_LENGTH;
    if (decodedLen < minDecoded || decodedLen > PACKET_LENGTH) {
        LOG_ERROR("packet_decompress: invalid decoded length=%lu", (unsigned long)decodedLen);
        return false;
    }

    const uLong payloadLen = decodedLen - SWITCH_PACKET_HASH_LENGTH;
    if (payloadLen > SWITCH_PACKET_DATA_LENGTH) {
        LOG_ERROR("packet_decompress: oversized payload=%lu", (unsigned long)payloadLen);
        return false;
    }

    p->dataLength = (u16)payloadLen;
    return true;
}

#endif
