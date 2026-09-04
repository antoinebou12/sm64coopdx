#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"

LUA_VERSION=5.3.5
LUA_SHA256=0c2eed3f960446e1a3e4b9a1ca2f3ff893b6ce41942cf54d5dd59ab4b3b058ac
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/new3ds-deps/lua"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/lua-${LUA_VERSION}"
OUTPUT_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/lua-${LUA_VERSION}.tar.gz"
URL="https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"

CC="${DEVKITARM}/bin/arm-none-eabi-gcc"
AR="${DEVKITARM}/bin/arm-none-eabi-ar"
RANLIB="${DEVKITARM}/bin/arm-none-eabi-ranlib"
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"

mkdir -p "${DOWNLOAD_DIR}" "${OUTPUT_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
    curl --fail --location --retry 3 --output "${TARBALL}" "${URL}"
fi

echo "${LUA_SHA256}  ${TARBALL}" | sha256sum --check --strict

rm -rf "${SOURCE_DIR}"
tar -xzf "${TARBALL}" -C "${BUILD_ROOT}"

# The parent CoopDX build disables make's built-in rules. Lua relies on its
# implicit .c -> .o rule, so deliberately clear inherited MAKEFLAGS here.
MAKEFLAGS=
make -C "${SOURCE_DIR}/src" clean
make -C "${SOURCE_DIR}/src" \
    CC="${CC}" \
    AR="${AR} rcu" \
    RANLIB="${RANLIB}" \
    MYCFLAGS="-O2 ${ARCH} -ffunction-sections -fdata-sections -DLUA_USE_C89" \
    MYLDFLAGS="" \
    liblua.a

cp "${SOURCE_DIR}/src/liblua.a" "${OUTPUT_DIR}/liblua53.a"
"${RANLIB}" "${OUTPUT_DIR}/liblua53.a"
sha256sum "${OUTPUT_DIR}/liblua53.a" | tee "${OUTPUT_DIR}/SHA256SUMS.txt"
