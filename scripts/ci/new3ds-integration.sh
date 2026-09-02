#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
export DEVKITPRO DEVKITARM
export PATH="${DEVKITPRO}/devkitARM/bin:${DEVKITPRO}/tools/bin:${PATH}"

# Host-generated dependency files (especially from Windows paths) break GNU make
# when the same workspace is bind-mounted into a Linux CI container.
find build/us_new3ds -name '*.d' -delete 2>/dev/null || true

make -f Makefile.new3ds-game new3ds-integration-smoke -j2

echo "New 3DS integration compile smoke passed"
