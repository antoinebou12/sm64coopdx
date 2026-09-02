#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
export DEVKITPRO DEVKITARM
export PATH="${DEVKITPRO}/devkitARM/bin:${DEVKITPRO}/tools/bin:${PATH}"

if [[ -z "${SM64_BASEROM_US_URL:-}" ]]; then
    echo "Set SM64_BASEROM_US_URL to a private HTTPS URL for your legally obtained US baserom." >&2
    exit 1
fi

curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
    "${SM64_BASEROM_US_URL}" -o baserom.us.z64
expected="$(awk 'NR==1 {print $1}' sm64.us.sha1)"
actual="$(sha1sum baserom.us.z64 | awk '{print $1}')"
test "$actual" = "$expected"
make -f Makefile.new3ds-game new3ds-3dsx -j2
rm -f baserom.us.z64
