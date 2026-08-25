#include "../coopnet_switch_identity.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMultiplier = 0xc6a4a7935bd1e995ULL;
constexpr unsigned int kShift = 47;
constexpr std::uint64_t kSeed = 0xc70f6907ULL;

int sFailures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++sFailures;
    }
}

std::uint64_t reference_hash(const std::vector<std::uint8_t>& data) {
    if (data.empty()) { return 0; }
    std::uint64_t hash = kSeed ^ (data.size() * kMultiplier);
    std::size_t offset = 0;
    while (offset + 8U <= data.size()) {
        std::uint64_t block = 0;
        for (unsigned int byte = 0; byte < 8U; ++byte) {
            block |= static_cast<std::uint64_t>(data[offset + byte]) << (byte * 8U);
        }
        block *= kMultiplier;
        block ^= block >> kShift;
        block *= kMultiplier;
        hash ^= block;
        hash *= kMultiplier;
        offset += 8U;
    }

    if (offset != data.size()) {
        std::uint64_t tail = 0;
        for (std::size_t byte = 0; offset + byte < data.size(); ++byte) {
            tail |= static_cast<std::uint64_t>(data[offset + byte]) << (byte * 8U);
        }
        hash ^= tail;
        hash *= kMultiplier;
    }
    hash ^= hash >> kShift;
    hash *= kMultiplier;
    hash ^= hash >> kShift;
    return hash;
}

std::vector<std::uint8_t> fixture(std::size_t size) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>((i * 131U + i / 7U + 0x5aU) & 0xffU);
    }
    return data;
}

void write_fixture(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    check(output.good(), "fixture write succeeds");
}

void test_size(const std::filesystem::path& directory, std::size_t size) {
    const auto path = directory / ("fixture-" + std::to_string(size) + ".bin");
    const auto data = fixture(size);
    write_fixture(path, data);
    const CoopNetSwitchIdentityResult result = coopnet_switch_hash_file(path.string().c_str());
    check(result.error == COOPNET_SWITCH_IDENTITY_OK, "non-empty fixture hashes successfully");
    check(result.bytes == size, "all fixture bytes are processed");
    check(result.hash == reference_hash(data), "streaming hash matches libstdc++ reference");
    const std::string libstdcxxInput(reinterpret_cast<const char*>(data.data()), data.size());
    check(result.hash == std::hash<std::string>{}(libstdcxxInput),
          "streaming hash matches std::hash<std::string>");
    const CoopNetSwitchIdentityResult again = coopnet_switch_hash_file(path.string().c_str());
    check(again.hash == result.hash, "file hash is deterministic");
}

} // namespace

int main() {
    const auto unique = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path()
        / ("sm64coopdx-switch-identity-" + unique);
    std::filesystem::create_directories(directory);

    const auto missing = coopnet_switch_hash_file((directory / "missing.nro").string().c_str());
    check(missing.hash == 0 && missing.error == COOPNET_SWITCH_IDENTITY_OPEN_FAILED,
          "missing files report open failure and zero hash");

    const auto recoverablePath = directory / "installed-later.nro";
    coopnet_switch_identity_reset_cache();
    const auto unavailable = coopnet_switch_identity(recoverablePath.string().c_str());
    check(!unavailable.cached && unavailable.hash == 0,
          "failed identity lookup is not cached");
    const auto installedLater = fixture(257U);
    write_fixture(recoverablePath, installedLater);
    const auto recovered = coopnet_switch_identity(recoverablePath.string().c_str());
    check(!recovered.cached && recovered.hash == reference_hash(installedLater),
          "identity lookup recovers after the NRO becomes available");

    const auto emptyPath = directory / "empty.nro";
    write_fixture(emptyPath, {});
    const auto empty = coopnet_switch_hash_file(emptyPath.string().c_str());
    check(empty.hash == 0 && empty.error == COOPNET_SWITCH_IDENTITY_EMPTY,
          "empty files report a specific zero-hash error");

    for (const std::size_t size : {
             1U, 7U, 8U, 9U,
             64U * 1024U - 1U,
             64U * 1024U,
             64U * 1024U + 1U,
             2U * 1024U * 1024U + 9U,
         }) {
        test_size(directory, size);
    }

    const auto cachePath = directory / "cache.nro";
    const auto original = fixture(4097U);
    write_fixture(cachePath, original);
    coopnet_switch_identity_reset_cache();
    const auto first = coopnet_switch_identity(cachePath.string().c_str());
    check(!first.cached && first.hash == reference_hash(original),
          "first successful identity read populates cache");

    const auto replacement = fixture(8193U);
    write_fixture(cachePath, replacement);
    const auto cached = coopnet_switch_identity(cachePath.string().c_str());
    check(cached.cached && cached.hash == first.hash && cached.bytes == first.bytes,
          "reconnect identity uses the successful cached fingerprint");

    coopnet_switch_identity_reset_cache();
    const auto refreshed = coopnet_switch_identity(cachePath.string().c_str());
    check(!refreshed.cached && refreshed.hash == reference_hash(replacement),
          "cache reset permits a fresh identity read");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError, "temporary fixtures are removed");

    if (sFailures != 0) {
        std::cerr << sFailures << " identity test(s) failed\n";
        return 1;
    }
    std::cout << "Switch CoopNet identity tests passed\n";
    return 0;
}
