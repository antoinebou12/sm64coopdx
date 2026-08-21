#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"

# CoopNet currently vendors the libjuice 1.6.2 API. Pin the matching upstream
# release commit so the Switch build never depends on a moving branch or a
# desktop prebuilt archive.
LIBJUICE_COMMIT="85efaa9b5e1cb3d4d534fc85d69cc9f7b76a66d7"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/switch-deps/libjuice"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/libjuice-${LIBJUICE_COMMIT}"
INCLUDE_DIR="${BUILD_ROOT}/include/juice"
LIB_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/libjuice-${LIBJUICE_COMMIT}.tar.gz"
URL="https://github.com/paullouisageneau/libjuice/archive/${LIBJUICE_COMMIT}.tar.gz"
PATCH_DIR="${ROOT_DIR}/tools/switch/patches/libjuice"

CROSS="${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-"
CC="${CROSS}gcc"
AR="${CROSS}ar"
RANLIB="${CROSS}ranlib"
READELF="${CROSS}readelf"
LIBNX="${DEVKITPRO}/libnx"

verify_aarch64_archive() {
    local archive="$1"
    local label="$2"
    local readelf_output=""
    local attempt=1

    # Cold -j2 builds can put enough concurrent I/O pressure on the dependency
    # tree for an immediately-following archive read to fail transiently. Keep
    # the validation strict, but retry the read instead of forcing users to
    # rebuild with -j1. Capturing readelf output also avoids the pipefail +
    # grep -q SIGPIPE false negative.
    while (( attempt <= 5 )); do
        if readelf_output="$("${READELF}" -h "${archive}" 2>&1)" &&
           grep -q "Machine:.*AArch64" <<< "${readelf_output}"; then
            return 0
        fi

        if (( attempt < 5 )); then
            echo "${label}: validation attempt ${attempt} failed; retrying..." >&2
            sync "${archive}" 2>/dev/null || true
            sleep 0.2
        fi
        attempt=$((attempt + 1))
    done

    echo "${label} is not a readable AArch64 archive after 5 attempts" >&2
    printf '%s\n' "${readelf_output}" >&2
    return 1
}

for tool in "${CC}" "${AR}" "${RANLIB}" "${READELF}" curl patch; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "Missing required tool: ${tool}" >&2
        exit 1
    }
done

mkdir -p "${DOWNLOAD_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
    curl --fail --location --retry 3 --output "${TARBALL}" "${URL}"
fi

rm -rf "${SOURCE_DIR}"
tar -xzf "${TARBALL}" -C "${BUILD_ROOT}"

for patch_file in "${PATCH_DIR}"/*.patch; do
    [[ -e "${patch_file}" ]] || continue
    patch --directory="${SOURCE_DIR}" -p1 --forward < "${patch_file}"
done

# Build only the static client library. Horizon uses libnx/newlib's POSIX socket
# and pthread compatibility layer. NO_IFADDRS selects libjuice's ioctl fallback;
# the dedicated ICE probe will validate candidate gathering on real hardware
# before CoopNet is enabled in the full game.
MAKEFLAGS=
make -C "${SOURCE_DIR}" dist-clean >/dev/null 2>&1 || true
make -C "${SOURCE_DIR}" libjuice.a \
    CC="${CC}" \
    AR="${AR}" \
    CFLAGS="-O2 -pthread -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-address-of-packed-member -D__SWITCH__ -DJUICE_STATIC -DJUICE_EXPORTS -DUSE_NETTLE=0 -DNO_SERVER -DNO_IFADDRS -DNO_PMTUDISC" \
    INCLUDES="-Iinclude/juice -I${LIBNX}/include"

cp "${SOURCE_DIR}/include/juice/juice.h" "${INCLUDE_DIR}/juice.h"
cp "${SOURCE_DIR}/libjuice.a" "${LIB_DIR}/libjuice.a"
"${RANLIB}" "${LIB_DIR}/libjuice.a"

sync "${LIB_DIR}/libjuice.a" 2>/dev/null || true
verify_aarch64_archive "${LIB_DIR}/libjuice.a" "libjuice.a"

printf '%s\n' "${LIBJUICE_COMMIT}" > "${BUILD_ROOT}/SOURCE_COMMIT.txt"
sha256sum "${LIB_DIR}/libjuice.a" | tee "${BUILD_ROOT}/SHA256SUMS.txt"
echo "Built Switch libjuice: ${LIB_DIR}/libjuice.a"
