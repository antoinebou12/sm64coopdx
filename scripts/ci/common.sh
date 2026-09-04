#!/usr/bin/env bash
# Shared helpers for console CI scripts (New 3DS + Switch).
set -euo pipefail

ci_root_dir() {
    cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

ci_export_devkitarm() {
    : "${DEVKITPRO:=/opt/devkitpro}"
    : "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
    export DEVKITPRO DEVKITARM
    export PATH="${DEVKITPRO}/devkitARM/bin:${DEVKITPRO}/tools/bin:${PATH}"
}

ci_export_devkita64() {
    : "${DEVKITPRO:=/opt/devkitpro}"
    export DEVKITPRO
    export PATH="${DEVKITPRO}/devkitA64/bin:${DEVKITPRO}/tools/bin:${PATH}"
}

ci_clean_dep_files() {
    local tree="$1"
    find "${tree}" -name '*.d' -delete 2>/dev/null || true
}

ci_fetch_us_baserom() {
    if [[ -z "${SM64_BASEROM_US_URL:-}" ]]; then
        echo "Set SM64_BASEROM_US_URL to a private HTTPS URL for your legally obtained US baserom." >&2
        exit 1
    fi

    curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
        "${SM64_BASEROM_US_URL}" -o baserom.us.z64

    local expected actual
    expected="$(awk 'NR==1 {print $1}' sm64.us.sha1)"
    actual="$(sha1sum baserom.us.z64 | awk '{print $1}')"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "baserom.us.z64 SHA-1 mismatch" >&2
        exit 1
    fi
}

ci_remove_us_baserom() {
    rm -f baserom.us.z64
}
