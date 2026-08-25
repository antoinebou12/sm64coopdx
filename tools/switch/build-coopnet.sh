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
IDENTITY_SOURCE="${ROOT_DIR}/tools/switch/coopnet_switch_identity.cpp"
IDENTITY_HEADER="${ROOT_DIR}/tools/switch/coopnet_switch_identity.hpp"
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

verify_aarch64_archive() {
    local archive="$1"
    local label="$2"
    local readelf_output=""
    local attempt=1

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

for identity_file in "${IDENTITY_SOURCE}" "${IDENTITY_HEADER}"; do
    if [[ ! -f "${identity_file}" ]]; then
        echo "Missing Switch CoopNet identity source: ${identity_file}" >&2
        exit 1
    fi
done

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

# Keep the implementation in the game repository while compiling it as part
# of the pinned CoopNet library. This also makes the exact host-tested source
# the source that runs on Horizon.
cp "${IDENTITY_SOURCE}" "${SOURCE_DIR}/common/coopnet_switch_identity.cpp"
cp "${IDENTITY_HEADER}" "${SOURCE_DIR}/common/coopnet_switch_identity.hpp"

while IFS= read -r patch_file; do
    patch --directory="${SOURCE_DIR}" -p1 --forward --ignore-whitespace < "${patch_file}"
done < <(find "${PATCH_DIR}" -maxdepth 1 -type f -name '*.patch' | sort -V)

"${PYTHON}" "${DIAG_PATCHER}" "${SOURCE_DIR}"

for marker in \
    'uint64_t coopnet_get_client_hash(void)' \
    'coopnet_switch_identity(filepath.c_str())'; do
    if ! grep -R -F -q "${marker}" "${SOURCE_DIR}/common"; then
        echo "ERROR: required CoopNet identity patch marker missing: ${marker}" >&2
        exit 1
    fi
done

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

if ! "${PYTHON}" - "${VENDORED_HEADER}" "${BUILT_HEADER}" <<'PY'
from pathlib import Path
import sys

vendored = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
built = Path(sys.argv[2]).read_text(encoding="utf-8").splitlines()
raise SystemExit(0 if vendored == built else 1)
PY
then
    echo "ERROR: CoopNet header drift detected." >&2
    echo "Vendored: ${VENDORED_HEADER}" >&2
    echo "Pinned:   ${BUILT_HEADER}" >&2
    diff -u "${VENDORED_HEADER}" "${BUILT_HEADER}" || true
    exit 1
fi

echo "Verified pinned CoopNet header matches vendored libcoopnet.h"

sync "${LIB_DIR}/libcoopnet.a" 2>/dev/null || true
verify_aarch64_archive "${LIB_DIR}/libcoopnet.a" "libcoopnet.a"

printf '%s\n' "${COOPNET_COMMIT}" > "${BUILD_ROOT}/SOURCE_COMMIT.txt"
sha256sum "${LIB_DIR}/libcoopnet.a" | tee "${BUILD_ROOT}/SHA256SUMS.txt"
echo "Built Switch CoopNet: ${LIB_DIR}/libcoopnet.a"
