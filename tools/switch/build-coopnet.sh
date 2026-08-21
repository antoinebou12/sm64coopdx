#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"

COOPNET_COMMIT="9d9b3dd4e87dba2fa3ca542ae32b73f43df32b0e"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/switch-deps/coopnet"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/coopnet-${COOPNET_COMMIT}"
OBJECT_DIR="${BUILD_ROOT}/obj"
INCLUDE_DIR="${BUILD_ROOT}/include"
LIB_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/coopnet-${COOPNET_COMMIT}.tar.gz"
URL="https://github.com/coop-deluxe/coopnet/archive/${COOPNET_COMMIT}.tar.gz"
PATCH_DIR="${ROOT_DIR}/tools/switch/patches/coopnet"
DIAG_PATCHER="${ROOT_DIR}/tools/switch/patch-coopnet-diagnostics.py"
LIBJUICE_ROOT="${ROOT_DIR}/build/switch-deps/libjuice"
VENDORED_HEADER="${ROOT_DIR}/lib/coopnet/include/libcoopnet.h"
BUILT_HEADER="${INCLUDE_DIR}/libcoopnet.h"
PYTHON="${PYTHON:-python3}"

CROSS="${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-"
CXX="${CROSS}g++"
AR="${CROSS}ar"
RANLIB="${CROSS}ranlib"
READELF="${CROSS}readelf"
LIBNX="${DEVKITPRO}/libnx"

for tool in "${CXX}" "${AR}" "${RANLIB}" "${READELF}" "${PYTHON}" curl patch cmp diff; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "Missing required tool: ${tool}" >&2
        exit 1
    }
done

if [[ ! -f "${DIAG_PATCHER}" ]]; then
    echo "Missing CoopNet diagnostics patcher: ${DIAG_PATCHER}" >&2
    exit 1
fi

if [[ ! -f "${LIBJUICE_ROOT}/lib/libjuice.a" ]]; then
    bash "${ROOT_DIR}/tools/switch/build-libjuice.sh"
fi

mkdir -p "${DOWNLOAD_DIR}" "${OBJECT_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
    curl --fail --location --retry 3 --output "${TARBALL}" "${URL}"
fi

rm -rf "${SOURCE_DIR}" "${OBJECT_DIR}"
mkdir -p "${OBJECT_DIR}"
tar -xzf "${TARBALL}" -C "${BUILD_ROOT}"

for patch_file in "${PATCH_DIR}"/*.patch; do
    [[ -e "${patch_file}" ]] || continue
    patch --directory="${SOURCE_DIR}" -p1 --forward < "${patch_file}"
done

"${PYTHON}" "${DIAG_PATCHER}" "${SOURCE_DIR}"

COMMON_FLAGS=(
    -O2
    -std=gnu++17
    -pthread
    -fPIC
    -ffunction-sections
    -fdata-sections
    -DJUICE_STATIC
    -D__SWITCH__
    -Wno-nonnull-compare
    -Wno-unused-function
    -I"${SOURCE_DIR}/common"
    -I"${LIBJUICE_ROOT}/include"
    -I"${SOURCE_DIR}/lib/include"
    -I"${LIBNX}/include"
)

objects=()
for source in "${SOURCE_DIR}"/common/*.cpp; do
    name="$(basename "${source}" .cpp)"
    object="${OBJECT_DIR}/${name}.o"
    "${CXX}" "${COMMON_FLAGS[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

"${AR}" rcs "${LIB_DIR}/libcoopnet.a" "${objects[@]}"
"${RANLIB}" "${LIB_DIR}/libcoopnet.a"
cp "${SOURCE_DIR}/common/libcoopnet.h" "${BUILT_HEADER}"

if [[ ! -f "${VENDORED_HEADER}" ]]; then
    echo "Missing vendored CoopNet header: ${VENDORED_HEADER}" >&2
    exit 1
fi

if ! cmp -s "${VENDORED_HEADER}" "${BUILT_HEADER}"; then
    echo "ERROR: CoopNet header drift detected." >&2
    echo "Vendored: ${VENDORED_HEADER}" >&2
    echo "Pinned:   ${BUILT_HEADER}" >&2
    diff -u "${VENDORED_HEADER}" "${BUILT_HEADER}" || true
    exit 1
fi

echo "Verified pinned CoopNet header matches vendored libcoopnet.h"

sync "${LIB_DIR}/libcoopnet.a" 2>/dev/null || true
# As with libjuice, avoid readelf | grep -q under pipefail. grep -q may close
# the pipe after the first match, causing readelf to exit on SIGPIPE and making
# a valid AArch64 archive look invalid.
if ! readelf_output="$("${READELF}" -h "${LIB_DIR}/libcoopnet.a" 2>&1)"; then
    echo "readelf failed for libcoopnet.a" >&2
    printf '%s\n' "${readelf_output}" >&2
    exit 1
fi
if ! grep -q "Machine:.*AArch64" <<< "${readelf_output}"; then
    echo "libcoopnet.a is not an AArch64 archive" >&2
    printf '%s\n' "${readelf_output}" >&2
    exit 1
fi

printf '%s\n' "${COOPNET_COMMIT}" > "${BUILD_ROOT}/SOURCE_COMMIT.txt"
sha256sum "${LIB_DIR}/libcoopnet.a" | tee "${BUILD_ROOT}/SHA256SUMS.txt"
echo "Built Switch CoopNet: ${LIB_DIR}/libcoopnet.a"
