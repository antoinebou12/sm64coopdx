#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"

LIBJUICE_COMMIT="85efaa9b5e1cb3d4d534fc85d69cc9f7b76a66d7"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/new3ds-deps/libjuice"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/libjuice-${LIBJUICE_COMMIT}"
INCLUDE_DIR="${BUILD_ROOT}/include/juice"
LIB_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/libjuice-${LIBJUICE_COMMIT}.tar.gz"
URL="https://github.com/paullouisageneau/libjuice/archive/${LIBJUICE_COMMIT}.tar.gz"
PATCH_DIR="${ROOT_DIR}/tools/switch/patches/libjuice"
NEW3DS_PATCH_DIR="${ROOT_DIR}/tools/new3ds/patches/libjuice"

CROSS="${DEVKITPRO}/devkitARM/bin/arm-none-eabi-"
CC="${CROSS}gcc"
AR="${CROSS}ar"
RANLIB="${CROSS}ranlib"
READELF="${CROSS}readelf"
CTRULIB="${DEVKITPRO}/libctru"

verify_arm_archive() {
    local archive="$1"
    local label="$2"
    local readelf_output=""
    local attempt=1

    while (( attempt <= 5 )); do
        if readelf_output="$("${READELF}" -h "${archive}" 2>&1)" &&
           grep -q "Machine:.*ARM" <<< "${readelf_output}"; then
            return 0
        fi

        if (( attempt < 5 )); then
            echo "${label}: validation attempt ${attempt} failed; retrying..." >&2
            sync "${archive}" 2>/dev/null || true
            sleep 0.2
        fi
        attempt=$((attempt + 1))
    done

    echo "${label} is not a readable ARM archive after 5 attempts" >&2
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
    sed 's/__SWITCH__/__3DS__/g' "${patch_file}" | patch --directory="${SOURCE_DIR}" -p1 --forward || true
done

for patch_file in "${NEW3DS_PATCH_DIR}"/*.patch; do
    [[ -e "${patch_file}" ]] || continue
    if ! patch --directory="${SOURCE_DIR}" -p1 --forward < "${patch_file}"; then
        echo "ERROR: failed to apply ${patch_file}" >&2
        exit 1
    fi
done

# The Switch random patch becomes invalid after __SWITCH__ -> __3DS__ substitution.
sed -i 's|#include <switch/kernel/random.h>|/* 3DS: use juice_random() rand() fallback */|' "${SOURCE_DIR}/src/random.c"
sed -i '/randomGet(buf, size);/c\	(void)buf; (void)size; return -1;' "${SOURCE_DIR}/src/random.c"

# libctru has no ifaddrs/ioctl interface enumeration; use gethostid() for IPv4 host candidates.
python3 - "${SOURCE_DIR}/src/udp.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
needle = "#else // NO_IFADDRS defined\n\tchar buf[4096];"
insert = """#else // NO_IFADDRS defined
#if defined(__3DS__)
	uint32_t host = gethostid();
	if (host != 0) {
		struct sockaddr_in sin;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = host;
		if (!has_duplicate_addr((struct sockaddr *)&sin, records, current - records)) {
			++ret;
			if (current != end) {
				memcpy(&current->addr, &sin, sizeof(sin));
				current->len = sizeof(sin);
				addr_set_port((struct sockaddr *)&current->addr, port);
				++current;
			}
		}
	}
#else
\tchar buf[4096];"""
if needle not in text:
    raise SystemExit(f"missing udp NO_IFADDRS anchor in {path}")
text = text.replace(needle, insert, 1)
close_needle = "\t}\n#endif\n#endif\n\n\treturn ret;"
close_insert = "\t}\n#endif /* !__3DS__ */\n#endif\n#endif\n\n\treturn ret;"
if close_needle not in text:
    raise SystemExit(f"missing udp NO_IFADDRS close anchor in {path}")
text = text.replace(close_needle, close_insert, 1)
path.write_text(text, encoding="utf-8")
PY

MAKEFLAGS=
make -C "${SOURCE_DIR}" dist-clean >/dev/null 2>&1 || true
make -C "${SOURCE_DIR}" libjuice.a \
    CC="${CC}" \
    AR="${AR}" \
    CFLAGS="-O2 -pthread -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-address-of-packed-member -D__3DS__ -DJUICE_STATIC -DJUICE_EXPORTS -DUSE_NETTLE=0 -DNO_SERVER -DNO_IFADDRS -DNO_PMTUDISC -include ${ROOT_DIR}/tools/new3ds/libjuice_3ds_compat.h" \
    INCLUDES="-Iinclude/juice -I${CTRULIB}/include"

cp "${SOURCE_DIR}/include/juice/juice.h" "${INCLUDE_DIR}/juice.h"
cp "${SOURCE_DIR}/libjuice.a" "${LIB_DIR}/libjuice.a"
"${RANLIB}" "${LIB_DIR}/libjuice.a"

sync "${LIB_DIR}/libjuice.a" 2>/dev/null || true
verify_arm_archive "${LIB_DIR}/libjuice.a" "libjuice.a"

printf '%s\n' "${LIBJUICE_COMMIT}" > "${BUILD_ROOT}/SOURCE_COMMIT.txt"
sha256sum "${LIB_DIR}/libjuice.a" | tee "${BUILD_ROOT}/SHA256SUMS.txt"
echo "Built New 3DS libjuice: ${LIB_DIR}/libjuice.a"
