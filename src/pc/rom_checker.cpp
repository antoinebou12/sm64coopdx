#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <cstring>
#include <cstdio>
#include <iomanip>

#if defined(_WIN32)
#include <windows.h>
#endif

extern "C" {
#include "platform.h"
#include "mods/mods_utils.h" // for path_ends_with
#include "mods/mod_cache.h"  // for md5 hashing
#include "mods/mods.h"
#include "loading.h"
#include "fs/fs.h"
}

namespace fs = std::filesystem;

bool gRomIsValid = false;
char gRomFilename[SYS_MAX_PATH] = "";

struct VanillaMD5 {
    const char *localizationName;
    const char *md5;
};

// lookup table for vanilla sm64 roms
static struct VanillaMD5 sVanillaMD5[] = {
    // { "eu", "45676429ef6b90e65b517129b700308e" },
    // { "jp", "85d61f5525af708c9f1e84dce6dc10e9" },
    // { "sh", "2d727c3278aa232d94f2fb45aec4d303" },
    { "us", "20b854b239203baf6c961b850a4a51a2" },
    { NULL, NULL },
};

inline static void rename_tmp_folder() {
#if defined(__SWITCH__) || defined(__3DS__)
    // Console ports use a fixed writable directory and should not run desktop
    // legacy-folder migration or std::filesystem work during startup.
    return;
#else
    std::string userPath = fs_get_write_path("");
    std::string oldPath = userPath + "tmp";
    std::string newPath = userPath + TMP_DIRECTORY;
    std::error_code ec;
    if (fs::exists(oldPath, ec) && !ec) {
        ec.clear();
        const bool newPathExists = fs::exists(newPath, ec);
        if (!ec && !newPathExists) {
#if defined(_WIN32)
            SetFileAttributesA(oldPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
            ec.clear();
            fs::rename(oldPath, newPath, ec);
        }
    }
#endif
}

#if defined(__SWITCH__) || defined(__3DS__)
#if defined(__3DS__)
extern "C" {
#include "pc/platform/new3ds/new3ds_log.h"
#include "pc/platform/new3ds/new3ds_boot_progress.h"
}
#endif

static char gRomSetupError[512] = "";

static void rom_set_setup_error(const char *message) {
    if (message == NULL) {
        gRomSetupError[0] = '\0';
        return;
    }
    snprintf(gRomSetupError, sizeof(gRomSetupError), "%s", message);
}

static void rom_log_setup_failure(const char *romPath, const char *reason) {
#if defined(__3DS__)
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "%s path=%s", reason, romPath != NULL ? romPath : "(null)");
    new3ds_log_flush();
#else
    (void)romPath;
    (void)reason;
#endif
}

#if defined(__3DS__)
static bool new3ds_rom_md5_cache_try_load(const char *cachePath, long expectedSize, u8 outHash[16]) {
    if (cachePath == NULL || cachePath[0] == '\0' || outHash == NULL) {
        return false;
    }
    FILE *cacheFile = fopen(cachePath, "rb");
    if (cacheFile == NULL) {
        return false;
    }
    char line[80] = { 0 };
    bool ok = false;
    if (fgets(line, sizeof(line), cacheFile) != NULL) {
        unsigned int bytes[16] = { 0 };
        long cachedSize = 0;
        if (sscanf(
                line,
                "%ld %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                &cachedSize,
                &bytes[0], &bytes[1], &bytes[2], &bytes[3],
                &bytes[4], &bytes[5], &bytes[6], &bytes[7],
                &bytes[8], &bytes[9], &bytes[10], &bytes[11],
                &bytes[12], &bytes[13], &bytes[14], &bytes[15]) == 17 &&
            cachedSize == expectedSize) {
            for (int i = 0; i < 16; ++i) {
                outHash[i] = (u8)bytes[i];
            }
            ok = true;
        }
    }
    fclose(cacheFile);
    return ok;
}

static void new3ds_rom_md5_cache_save(const char *cachePath, long size, const u8 hash[16]) {
    if (cachePath == NULL || cachePath[0] == '\0' || hash == NULL) {
        return;
    }
    FILE *cacheFile = fopen(cachePath, "wb");
    if (cacheFile == NULL) {
        return;
    }
    fprintf(
        cacheFile,
        "%ld %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
        size,
        hash[0], hash[1], hash[2], hash[3],
        hash[4], hash[5], hash[6], hash[7],
        hash[8], hash[9], hash[10], hash[11],
        hash[12], hash[13], hash[14], hash[15]);
    fclose(cacheFile);
}

static void new3ds_rom_md5_hex(const u8 hash[16], char out[33]) {
    static const char *kHex = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        out[i * 2] = kHex[(hash[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[hash[i] & 0xF];
    }
    out[32] = '\0';
}

typedef enum New3dsRomEndian {
    NEW3DS_ROM_ENDIAN_Z64 = 0,
    NEW3DS_ROM_ENDIAN_V64,
    NEW3DS_ROM_ENDIAN_N64,
    NEW3DS_ROM_ENDIAN_UNKNOWN,
} New3dsRomEndian;

static New3dsRomEndian new3ds_rom_detect_endian(const u8 header[4]) {
    if (header[0] == 0x80 && header[1] == 0x37 && header[2] == 0x12 && header[3] == 0x40) {
        return NEW3DS_ROM_ENDIAN_Z64;
    }
    if (header[0] == 0x37 && header[1] == 0x80 && header[2] == 0x40 && header[3] == 0x12) {
        return NEW3DS_ROM_ENDIAN_V64;
    }
    if (header[0] == 0x40 && header[1] == 0x12 && header[2] == 0x37 && header[3] == 0x80) {
        return NEW3DS_ROM_ENDIAN_N64;
    }
    return NEW3DS_ROM_ENDIAN_UNKNOWN;
}

static const char *new3ds_rom_endian_name(New3dsRomEndian endian) {
    switch (endian) {
        case NEW3DS_ROM_ENDIAN_Z64: return "z64";
        case NEW3DS_ROM_ENDIAN_V64: return "v64-byteswap";
        case NEW3DS_ROM_ENDIAN_N64: return "n64-wordswap";
        default: return "unknown";
    }
}

static void new3ds_rom_convert_chunk(u8 *buf, size_t len, New3dsRomEndian endian) {
    if (endian == NEW3DS_ROM_ENDIAN_V64) {
        for (size_t i = 0; i + 1 < len; i += 2) {
            u8 tmp = buf[i];
            buf[i] = buf[i + 1];
            buf[i + 1] = tmp;
        }
        return;
    }
    if (endian == NEW3DS_ROM_ENDIAN_N64) {
        for (size_t i = 0; i + 3 < len; i += 4) {
            u8 a = buf[i];
            u8 b = buf[i + 1];
            buf[i] = buf[i + 3];
            buf[i + 1] = buf[i + 2];
            buf[i + 2] = b;
            buf[i + 3] = a;
        }
    }
}

static bool new3ds_rom_rewrite_native(const char *romPath, New3dsRomEndian endian) {
    if (romPath == NULL || endian == NEW3DS_ROM_ENDIAN_Z64 || endian == NEW3DS_ROM_ENDIAN_UNKNOWN) {
        return false;
    }

    char tempPath[SYS_MAX_PATH];
    const int written = snprintf(tempPath, sizeof(tempPath), "%s.convert", romPath);
    if (written < 0 || (size_t)written >= sizeof(tempPath)) {
        return false;
    }

    FILE *inFile = fopen(romPath, "rb");
    if (inFile == NULL) {
        return false;
    }
    FILE *outFile = fopen(tempPath, "wb");
    if (outFile == NULL) {
        fclose(inFile);
        return false;
    }

    u8 buffer[64 * 1024];
    bool ok = true;
    while (ok) {
        const size_t readBytes = fread(buffer, 1, sizeof(buffer), inFile);
        if (readBytes == 0) {
            break;
        }
        if ((endian == NEW3DS_ROM_ENDIAN_V64 && (readBytes % 2) != 0) ||
            (endian == NEW3DS_ROM_ENDIAN_N64 && (readBytes % 4) != 0)) {
            ok = false;
            break;
        }
        new3ds_rom_convert_chunk(buffer, readBytes, endian);
        if (fwrite(buffer, 1, readBytes, outFile) != readBytes) {
            ok = false;
            break;
        }
        new3ds_boot_progress_pump();
    }

    if (ferror(inFile)) {
        ok = false;
    }
    fclose(inFile);
    if (fclose(outFile) != 0) {
        ok = false;
    }

    if (!ok) {
        remove(tempPath);
        return false;
    }

    if (remove(romPath) != 0) {
        remove(tempPath);
        return false;
    }
    if (rename(tempPath, romPath) != 0) {
        return false;
    }
    return true;
}

static const char *new3ds_rom_match_known_region(const char *md5Hex) {
    static const struct {
        const char *name;
        const char *md5;
    } kRegions[] = {
        { "EU", "45676429ef6b90e65b517129b700308e" },
        { "JP", "85d61f5525af708c9f1e84dce6dc10e9" },
        { "Shindou", "2d727c3278aa232d94f2fb45aec4d303" },
        { NULL, NULL },
    };
    for (int i = 0; kRegions[i].name != NULL; ++i) {
        if (strcmp(md5Hex, kRegions[i].md5) == 0) {
            return kRegions[i].name;
        }
    }
    return NULL;
}

static void new3ds_rom_fail_invalid(
    const char *romPath,
    long romSize,
    New3dsRomEndian endian,
    const u8 computedHash[16]) {
    static const char *kExpected = "20b854b239203baf6c961b850a4a51a2";
    char computedHex[33];
    new3ds_rom_md5_hex(computedHash, computedHex);
    const char *region = new3ds_rom_match_known_region(computedHex);

    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "invalid hash");
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "path=%s", romPath != NULL ? romPath : "(null)");
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "size=%ld format=%s", romSize, new3ds_rom_endian_name(endian));
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "expected=%s", kExpected);
    NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "computed=%s", computedHex);
    if (region != NULL) {
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "looks_like=%s", region);
    }
    new3ds_log_flush();

    if (region != NULL) {
        snprintf(
            gRomSetupError,
            sizeof(gRomSetupError),
            "ERROR: wrong ROM region (%s).\n\n"
            "Need vanilla US .z64\n"
            "MD5 %s\n\n"
            "Got %s\n"
            "sdmc:/3ds/sm64coopdx/\n"
            "baserom.us.z64",
            region,
            kExpected,
            computedHex);
    } else if (endian == NEW3DS_ROM_ENDIAN_V64 || endian == NEW3DS_ROM_ENDIAN_N64) {
        snprintf(
            gRomSetupError,
            sizeof(gRomSetupError),
            "ERROR: byte-swapped ROM could\n"
            "not be converted to US .z64.\n\n"
            "Need MD5 %s\n"
            "Got %s",
            kExpected,
            computedHex);
    } else {
        snprintf(
            gRomSetupError,
            sizeof(gRomSetupError),
            "ERROR: baserom.us.z64 invalid.\n\n"
            "Need vanilla US .z64\n"
            "MD5 %s\n\n"
            "Got %s\n"
            "format=%s size=%ld",
            kExpected,
            computedHex,
            new3ds_rom_endian_name(endian),
            romSize);
    }

    new3ds_boot_progress_set("ROM hash failed - see error");
}
#endif

static bool is_console_rom_valid(void) {
    static const u8 sUsRomMd5[16] = {
        0x20, 0xb8, 0x54, 0xb2, 0x39, 0x20, 0x3b, 0xaf,
        0x6c, 0x96, 0x1b, 0x85, 0x0a, 0x4a, 0x51, 0xa2,
    };
#if defined(__3DS__)
    static const long sUsRomSize = 8388608L;
#endif

    // fs_get_write_path() returns a static buffer. Copy immediately so later
    // calls (MD5 cache path, logs) cannot overwrite the ROM pathname.
    char romPath[SYS_MAX_PATH] = { 0 };
    {
        const char *romPathTmp = fs_get_write_path("baserom.us.z64");
        if (romPathTmp != nullptr) {
            snprintf(romPath, sizeof(romPath), "%s", romPathTmp);
        }
    }
    rom_set_setup_error(NULL);
    if (romPath[0] == '\0' || !fs_sys_file_exists(romPath)) {
#if defined(__3DS__)
        rom_set_setup_error(
            "ERROR: baserom.us.z64 is missing.\n\n"
            "Place the US ROM at:\n"
            "sdmc:/3ds/sm64coopdx/baserom.us.z64");
        new3ds_boot_progress_set("ROM missing");
#elif defined(__SWITCH__)
        rom_set_setup_error(
            "ERROR: baserom.us.z64 is missing.\n\n"
            "Place the US ROM at:\n"
            "sdmc:/switch/sm64coopdx/baserom.us.z64");
#endif
        rom_log_setup_failure(romPath, "missing baserom.us.z64");
        return false;
    }

#if defined(__3DS__)
    long romSize = 0;
    New3dsRomEndian endian = NEW3DS_ROM_ENDIAN_UNKNOWN;
    {
        FILE *romFile = fopen(romPath, "rb");
        if (romFile == NULL) {
            rom_set_setup_error("ERROR: could not open baserom.us.z64.");
            rom_log_setup_failure(romPath, "fopen failed");
            new3ds_boot_progress_set("ROM open failed");
            return false;
        }
        u8 header[4] = { 0 };
        if (fread(header, 1, 4, romFile) != 4) {
            fclose(romFile);
            rom_set_setup_error("ERROR: could not read baserom.us.z64 header.");
            rom_log_setup_failure(romPath, "header read failed");
            new3ds_boot_progress_set("ROM header failed");
            return false;
        }
        endian = new3ds_rom_detect_endian(header);
        if (fseek(romFile, 0, SEEK_END) != 0) {
            fclose(romFile);
            rom_set_setup_error("ERROR: could not read baserom.us.z64 size.");
            rom_log_setup_failure(romPath, "fseek failed");
            new3ds_boot_progress_set("ROM size failed");
            return false;
        }
        romSize = ftell(romFile);
        fclose(romFile);
        if (romSize != sUsRomSize) {
            rom_set_setup_error(
                "ERROR: baserom.us.z64 has the wrong size.\n\n"
                "Expected an 8 MiB vanilla US ROM.");
            rom_log_setup_failure(romPath, "wrong size");
            new3ds_boot_progress_set("ROM wrong size");
            return false;
        }
        NEW3DS_LOG_INFO_CAT(
            NEW3DS_LOG_CAT_RUNTIME,
            "rom",
            "size=%ld format=%s",
            romSize,
            new3ds_rom_endian_name(endian));
    }

    if (endian == NEW3DS_ROM_ENDIAN_V64 || endian == NEW3DS_ROM_ENDIAN_N64) {
        new3ds_boot_progress_set("Converting byte-swapped ROM...");
        NEW3DS_LOG_INFO_CAT(
            NEW3DS_LOG_CAT_RUNTIME,
            "rom",
            "converting %s -> z64",
            new3ds_rom_endian_name(endian));
        if (!new3ds_rom_rewrite_native(romPath, endian)) {
            rom_set_setup_error(
                "ERROR: failed to convert byte-swapped\n"
                "ROM to native .z64.\n\n"
                "Check SD free space and retry.");
            rom_log_setup_failure(romPath, "byteswap convert failed");
            new3ds_boot_progress_set("ROM convert failed");
            return false;
        }
        endian = NEW3DS_ROM_ENDIAN_Z64;
        const char *staleCache = fs_get_write_path("baserom.us.z64.md5");
        if (staleCache != NULL && staleCache[0] != '\0') {
            remove(staleCache);
        }
    }

    {
        char cachePath[SYS_MAX_PATH] = { 0 };
        {
            const char *cachePathTmp = fs_get_write_path("baserom.us.z64.md5");
            if (cachePathTmp != nullptr) {
                snprintf(cachePath, sizeof(cachePath), "%s", cachePathTmp);
            }
        }
        u8 dataHash[16] = { 0 };
        if (!(new3ds_rom_md5_cache_try_load(cachePath, sUsRomSize, dataHash) &&
              memcmp(dataHash, sUsRomMd5, sizeof(sUsRomMd5)) == 0)) {
            new3ds_boot_progress_set("Hashing ROM (one-time)...");
            NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "hashing %s", romPath);
            if (!mod_cache_md5(romPath, dataHash)) {
                rom_set_setup_error(
                    "ERROR: could not read baserom.us.z64\n"
                    "to compute MD5.\n\n"
                    "Check the SD path:\n"
                    "sdmc:/3ds/sm64coopdx/baserom.us.z64");
                rom_log_setup_failure(romPath, "md5 fopen/read failed");
                new3ds_boot_progress_set("ROM read failed");
                return false;
            }
            if (memcmp(dataHash, sUsRomMd5, sizeof(sUsRomMd5)) != 0) {
                new3ds_rom_fail_invalid(romPath, romSize, endian, dataHash);
                return false;
            }
            new3ds_rom_md5_cache_save(cachePath, sUsRomSize, dataHash);
        } else {
            NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "md5 cache hit");
        }
    }
#else
    u8 dataHash[16] = { 0 };
    if (!mod_cache_md5(romPath, dataHash)) {
        rom_set_setup_error(
            "ERROR: could not read baserom.us.z64\n"
            "to compute MD5.");
        rom_log_setup_failure(romPath, "md5 fopen/read failed");
        return false;
    }
    if (memcmp(dataHash, sUsRomMd5, sizeof(sUsRomMd5)) != 0) {
        rom_set_setup_error(
            "ERROR: baserom.us.z64 is invalid.\n\n"
            "The file must be a vanilla\n"
            "US Super Mario 64 ROM.");
        rom_log_setup_failure(romPath, "invalid baserom.us.z64 hash");
        return false;
    }
#endif

    const int written = snprintf(gRomFilename, sizeof(gRomFilename), "%s", romPath);
    if (written < 0 || (size_t)written >= sizeof(gRomFilename)) {
        gRomFilename[0] = '\0';
        rom_set_setup_error("ERROR: baserom.us.z64 path is too long.");
        rom_log_setup_failure(romPath, "baserom path too long");
        return false;
    }

    gRomIsValid = true;
#if defined(__3DS__)
    NEW3DS_LOG_INFO_CAT(NEW3DS_LOG_CAT_RUNTIME, "rom", "loaded %s", gRomFilename);
    new3ds_log_flush();
    new3ds_boot_progress_set("ROM OK");
#endif
    return true;
}

#else

static bool is_rom_valid(const std::string romPath) {
    u8 dataHash[16] = { 0 };
    if (!mod_cache_md5(romPath.c_str(), dataHash)) {
        return false;
    }

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(dataHash[i]);
    }

    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        if (md5->md5 == ss.str()) {
            std::string destPath = fs_get_write_path("") + std::string("baserom.") + md5->localizationName + ".z64";

            // Copy the rom to the user path. Filesystem failures are handled as
            // a rejected ROM instead of terminating the process with an exception.
            std::error_code ec;
            const bool destinationExists = fs::exists(fs::path(destPath), ec);
            if (ec) { return false; }
            if (romPath != destPath && !destinationExists) {
                fs::copy_file(fs::path(romPath), fs::path(destPath), ec);
                if (ec) { return false; }
            }

            snprintf(gRomFilename, SYS_MAX_PATH, "%s", destPath.c_str()); // Load the copied rom
            gRomIsValid = true;
            return true;
        }
    }

    return false;
}

inline static bool scan_path_for_rom(const char *dir) {
    if (dir == nullptr || dir[0] == '\0') { return false; }

    std::error_code ec;
    fs::path directory(dir);
    if (!fs::exists(directory, ec) || ec) { return false; }
    ec.clear();
    if (!fs::is_directory(directory, ec) || ec) { return false; }

    fs::directory_iterator it(directory, ec);
    const fs::directory_iterator end;
    while (!ec && it != end) {
        std::string path = it->path().generic_string();
        if (path_ends_with(path.c_str(), ".z64") && is_rom_valid(path)) {
            return true;
        }
        it.increment(ec);
    }
    return false;
}
#endif

extern "C" {
void legacy_folder_handler(void) {
    rename_tmp_folder();
}

#if defined(__SWITCH__) || defined(__3DS__)
const char *rom_get_setup_error(void) {
    return gRomSetupError[0] != '\0' ? gRomSetupError : NULL;
}
#endif

bool main_rom_handler(void) {
#if defined(__SWITCH__) || defined(__3DS__)
    return is_console_rom_valid();
#else
    if (scan_path_for_rom(fs_get_write_path(""))) { return true; }
    scan_path_for_rom(sys_exe_path_dir());
    return gRomIsValid;
#endif
}

void rom_on_drop_file(const char *path) {
#if defined(__SWITCH__) || defined(__3DS__)
    (void)path;
#else
    static bool hasDroppedInvalidFile = false;
    if (strlen(path) > 0 && !is_rom_valid(path) && !hasDroppedInvalidFile) {
        hasDroppedInvalidFile = true;
        strcat(gCurrLoadingSegment.str, "\n\\#ffc000\\The file you last dropped was not a valid, vanilla SM64 rom.");
    }
#endif
}
}
