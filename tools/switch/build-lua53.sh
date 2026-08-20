#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"

LUA_VERSION=5.3.5
LUA_SHA256=0c2eed3f960446e1a3e4b9a1ca2f3ff893b6ce41942cf54d5dd59ab4b3b058ac
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/switch-deps/lua"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/lua-${LUA_VERSION}"
OUTPUT_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/lua-${LUA_VERSION}.tar.gz"
URL="https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"

CC="${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc"
AR="${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-ar"
RANLIB="${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-ranlib"

mkdir -p "${DOWNLOAD_DIR}" "${OUTPUT_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
    curl --fail --location --retry 3 --output "${TARBALL}" "${URL}"
fi

echo "${LUA_SHA256}  ${TARBALL}" | sha256sum --check --strict

rm -rf "${SOURCE_DIR}"
tar -xzf "${TARBALL}" -C "${BUILD_ROOT}"

# Vanilla lua's Makefile has no explicit .c.o rule; it relies on make's
# built-in one. Makefile.sdl3 sets MAKEFLAGS += --no-builtin-rules for the
# main build, and that propagates to this nested make via the environment,
# which silently drops every .o without a single compiler invocation. Clear
# it here so lua gets make's default implicit rules back.
MAKEFLAGS=
make -C "${SOURCE_DIR}/src" clean
# LUA_USE_POSIX is deliberately omitted: devkitA64's newlib does not declare
# _setjmp/_longjmp under any of the usual feature-test macros, and Horizon has
# no shell for os.execute/popen anyway. Lua falls back to its portable ISO C
# setjmp/longjmp path instead.
make -C "${SOURCE_DIR}/src" \
    CC="${CC}" \
    AR="${AR} rcu" \
    RANLIB="${RANLIB}" \
    MYCFLAGS="-O2 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ffunction-sections -fdata-sections" \
    MYLDFLAGS="" \
    liblua.a

cp "${SOURCE_DIR}/src/liblua.a" "${OUTPUT_DIR}/liblua53.a"
"${RANLIB}" "${OUTPUT_DIR}/liblua53.a"
sha256sum "${OUTPUT_DIR}/liblua53.a" | tee "${OUTPUT_DIR}/SHA256SUMS.txt"
