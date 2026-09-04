#include "coopnet_new3ds_identity.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

namespace {

constexpr std::uint64_t kMurmurMultiplier = 0xc6a4a7935bd1e995ULL;
constexpr unsigned int kMurmurShift = 47;
constexpr std::uint64_t kLibstdcxxSeed = 0xc70f6907ULL;
constexpr std::size_t kReadChunkSize = 64U * 1024U;

class Murmur64AStream {
public:
    explicit Murmur64AStream(const std::uint64_t totalLength)
        : mHash(kLibstdcxxSeed ^ (totalLength * kMurmurMultiplier)) {
    }

    void update(const std::uint8_t* data, std::size_t size) {
        if (mTailSize != 0) {
            const std::size_t needed = 8U - mTailSize;
            const std::size_t copied = std::min(needed, size);
            std::copy_n(data, copied, mTail.begin() + static_cast<std::ptrdiff_t>(mTailSize));
            mTailSize += copied;
            data += copied;
            size -= copied;
            if (mTailSize == 8U) {
                mix_block(load_little_endian(mTail.data()));
                mTailSize = 0;
            }
        }

        while (size >= 8U) {
            mix_block(load_little_endian(data));
            data += 8U;
            size -= 8U;
        }

        if (size != 0) {
            std::copy_n(data, size, mTail.begin());
            mTailSize = size;
        }
    }

    [[nodiscard]] std::uint64_t finish() const {
        std::uint64_t hash = mHash;
        switch (mTailSize) {
            case 7: hash ^= static_cast<std::uint64_t>(mTail[6]) << 48U; [[fallthrough]];
            case 6: hash ^= static_cast<std::uint64_t>(mTail[5]) << 40U; [[fallthrough]];
            case 5: hash ^= static_cast<std::uint64_t>(mTail[4]) << 32U; [[fallthrough]];
            case 4: hash ^= static_cast<std::uint64_t>(mTail[3]) << 24U; [[fallthrough]];
            case 3: hash ^= static_cast<std::uint64_t>(mTail[2]) << 16U; [[fallthrough]];
            case 2: hash ^= static_cast<std::uint64_t>(mTail[1]) << 8U; [[fallthrough]];
            case 1:
                hash ^= static_cast<std::uint64_t>(mTail[0]);
                hash *= kMurmurMultiplier;
                break;
            case 0:
                break;
        }

        hash ^= hash >> kMurmurShift;
        hash *= kMurmurMultiplier;
        hash ^= hash >> kMurmurShift;
        return hash;
    }

private:
    static std::uint64_t load_little_endian(const std::uint8_t* data) {
        std::uint64_t value = 0;
        for (unsigned int i = 0; i < 8U; ++i) {
            value |= static_cast<std::uint64_t>(data[i]) << (i * 8U);
        }
        return value;
    }

    void mix_block(std::uint64_t block) {
        block *= kMurmurMultiplier;
        block ^= block >> kMurmurShift;
        block *= kMurmurMultiplier;
        mHash ^= block;
        mHash *= kMurmurMultiplier;
    }

    std::uint64_t mHash;
    std::array<std::uint8_t, 8> mTail{};
    std::size_t mTailSize = 0;
};

std::mutex sIdentityMutex;
bool sIdentityCached = false;
std::string sIdentityPath;
CoopNetNew3dsIdentityResult sIdentityResult;

} // namespace

CoopNetNew3dsIdentityResult coopnet_new3ds_hash_file(const char* path) {
    CoopNetNew3dsIdentityResult result;
    if (path == nullptr || path[0] == '\0') {
        result.error = COOPNET_NEW3DS_IDENTITY_OPEN_FAILED;
        return result;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        result.error = COOPNET_NEW3DS_IDENTITY_OPEN_FAILED;
        return result;
    }

    file.seekg(0, std::ios::end);
    const std::streampos end = file.tellg();
    if (end < 0) {
        result.error = COOPNET_NEW3DS_IDENTITY_SIZE_FAILED;
        return result;
    }

    const auto unsignedEnd = static_cast<std::uint64_t>(end);
    if (unsignedEnd == 0) {
        result.error = COOPNET_NEW3DS_IDENTITY_EMPTY;
        return result;
    }
    if (unsignedEnd > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        result.error = COOPNET_NEW3DS_IDENTITY_SIZE_FAILED;
        return result;
    }

    file.seekg(0, std::ios::beg);
    if (!file.good()) {
        result.error = COOPNET_NEW3DS_IDENTITY_SIZE_FAILED;
        return result;
    }

    Murmur64AStream hasher(unsignedEnd);
    std::array<std::uint8_t, kReadChunkSize> buffer{};
    while (result.bytes < unsignedEnd) {
        const std::uint64_t remaining = unsignedEnd - result.bytes;
        const auto requested = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        file.read(reinterpret_cast<char*>(buffer.data()), requested);
        const std::streamsize received = file.gcount();
        if (received <= 0) {
            result.error = COOPNET_NEW3DS_IDENTITY_READ_FAILED;
            result.hash = 0;
            return result;
        }

        hasher.update(buffer.data(), static_cast<std::size_t>(received));
        result.bytes += static_cast<std::uint64_t>(received);
    }

    if (result.bytes != unsignedEnd) {
        result.error = COOPNET_NEW3DS_IDENTITY_READ_FAILED;
        result.hash = 0;
        return result;
    }

    result.hash = hasher.finish();
    result.error = result.hash == 0
        ? COOPNET_NEW3DS_IDENTITY_READ_FAILED
        : COOPNET_NEW3DS_IDENTITY_OK;
    return result;
}

CoopNetNew3dsIdentityResult coopnet_new3ds_identity(const char* path) {
    const std::string requestedPath = path != nullptr ? path : "";
    std::lock_guard<std::mutex> lock(sIdentityMutex);
    if (sIdentityCached && sIdentityPath == requestedPath) {
        CoopNetNew3dsIdentityResult cached = sIdentityResult;
        cached.cached = true;
        return cached;
    }

    CoopNetNew3dsIdentityResult result = coopnet_new3ds_hash_file(path);
    if (result.error == COOPNET_NEW3DS_IDENTITY_OK && result.hash != 0) {
        sIdentityPath = requestedPath;
        sIdentityResult = result;
        sIdentityCached = true;
    }
    return result;
}

void coopnet_new3ds_identity_reset_cache(void) {
    std::lock_guard<std::mutex> lock(sIdentityMutex);
    sIdentityCached = false;
    sIdentityPath.clear();
    sIdentityResult = {};
}
