#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO must point at the devkitPro root}"

COOPNET_COMMIT="9d9b3dd4e87dba2fa3ca542ae32b73f43df32b0e"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/new3ds-deps/coopnet"
DOWNLOAD_DIR="${BUILD_ROOT}/download"
SOURCE_DIR="${BUILD_ROOT}/coopnet-${COOPNET_COMMIT}"
OBJECT_DIR="${BUILD_ROOT}/obj"
INCLUDE_DIR="${BUILD_ROOT}/include"
LIB_DIR="${BUILD_ROOT}/lib"
TARBALL="${DOWNLOAD_DIR}/coopnet-${COOPNET_COMMIT}.tar.gz"
URL="https://github.com/coop-deluxe/coopnet/archive/${COOPNET_COMMIT}.tar.gz"
PATCH_DIR="${ROOT_DIR}/tools/switch/patches/coopnet"
DIAG_PATCHER="${ROOT_DIR}/tools/switch/patch-coopnet-diagnostics.py"
IDENTITY_SOURCE="${ROOT_DIR}/tools/new3ds/coopnet_new3ds_identity.cpp"
IDENTITY_HEADER="${ROOT_DIR}/tools/new3ds/coopnet_new3ds_identity.hpp"
LIBJUICE_ROOT="${ROOT_DIR}/build/new3ds-deps/libjuice"
VENDORED_HEADER="${ROOT_DIR}/lib/coopnet/include/libcoopnet.h"
BUILT_HEADER="${INCLUDE_DIR}/libcoopnet.h"
PYTHON="${PYTHON:-python3}"

CROSS="${DEVKITPRO}/devkitARM/bin/arm-none-eabi-"
CXX="${CROSS}g++"
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
        echo "Missing New 3DS CoopNet identity source: ${identity_file}" >&2
        exit 1
    fi
done

if [[ ! -f "${LIBJUICE_ROOT}/lib/libjuice.a" ]]; then
    bash "${ROOT_DIR}/tools/new3ds/build-libjuice.sh"
fi

mkdir -p "${DOWNLOAD_DIR}" "${OBJECT_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
    curl --fail --location --retry 3 --output "${TARBALL}" "${URL}"
fi

rm -rf "${SOURCE_DIR}" "${OBJECT_DIR}"
mkdir -p "${OBJECT_DIR}"
tar -xzf "${TARBALL}" -C "${BUILD_ROOT}"

cp "${IDENTITY_SOURCE}" "${SOURCE_DIR}/common/coopnet_new3ds_identity.cpp"
cp "${IDENTITY_HEADER}" "${SOURCE_DIR}/common/coopnet_new3ds_identity.hpp"

while IFS= read -r patch_file; do
    case "$(basename "${patch_file}")" in
        0005-horizon-bind-active-ip.patch)
            echo "Skipping Switch-only patch: $(basename "${patch_file}")"
            continue
            ;;
    esac
    sed 's/__SWITCH__/__3DS__/g;
         s/coopnet_switch_identity/coopnet_new3ds_identity/g;
         s/COOPNET_SWITCH_NRO_PATH/COOPNET_NEW3DS_APP_PATH/g;
         s/coopnet_switch_identity.hpp/coopnet_new3ds_identity.hpp/g;
         s/CoopNetSwitchIdentity/CoopNetNew3dsIdentity/g;
         s/COOPNET_SWITCH_IDENTITY/COOPNET_NEW3DS_IDENTITY/g' \
        "${patch_file}" | patch --directory="${SOURCE_DIR}" -p1 --forward --ignore-whitespace || true
done < <(find "${PATCH_DIR}" -maxdepth 1 -type f -name '*.patch' | sort -V)

"${PYTHON}" "${DIAG_PATCHER}" "${SOURCE_DIR}"
find "${SOURCE_DIR}" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 |
    while IFS= read -r -d '' file; do
        sed -i 's/__SWITCH__/__3DS__/g' "${file}"
        sed -i 's/<switch\.h>/<3ds.h>/g' "${file}"
    done

"${PYTHON}" - "${SOURCE_DIR}/common/peer.cpp" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

# Remove accidental includes injected inside the Peer constructor.
text = re.sub(
    r"#ifdef __3DS__\n#include <3ds.h>\n#include <arpa/inet.h>\n",
    "#ifdef __3DS__\n",
    text,
)

if "#include <3ds.h>" not in text.split("Peer::Peer")[0]:
    text = text.replace(
        "#include <cerrno>\n",
        "#include <cerrno>\n#ifdef __3DS__\n#include <3ds.h>\n#include <arpa/inet.h>\n#include <netinet/in.h>\n#endif\n",
        1,
    )

text = re.sub(
    r"#ifdef __3DS__\n    config\.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;\n#else\n#ifdef __3DS__\n    config\.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;\n#else\n    config\.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;\n#endif\n#endif",
    "#ifdef __3DS__\n    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;\n#else\n    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;\n#endif",
    text,
    count=1,
)

needle = "    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;\n"
threaded = """#ifdef __3DS__
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
#else
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
#endif
"""
if needle in text:
    text = text.replace(needle, threaded, 1)

bind_block = """
#ifdef __3DS__
    {
        char bindAddress[INET_ADDRSTRLEN] = { 0 };
        const uint32_t host = gethostid();
        if (host != 0 &&
            inet_ntop(AF_INET, &host, bindAddress, sizeof(bindAddress))) {
            config.bind_address = bindAddress;
            LOG_INFO("New 3DS ICE bind address: %s", bindAddress);
            horizon_diag("ice.bind_address", mId, (uint64_t)ntohl(host), bindAddress);
        } else {
            horizon_diag("ice.bind_address_failed", mId, (uint64_t)host);
        }
    }
#endif
"""
marker = "    mConnected = false;\n"
if "New 3DS ICE bind address" not in text:
    if marker not in text:
        raise SystemExit(f"missing mConnected anchor in {path}")
    text = text.replace(marker, bind_block + marker, 1)

path.write_text(text, encoding="utf-8")
PY

"${PYTHON}" - "${SOURCE_DIR}/common/server.cpp" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text = re.sub(
    r"clamp\(mReputation\[aDestinationId\]\.value ([+-]) (\d+), -16, 16\)",
    r"clamp<int32_t>(mReputation[aDestinationId].value \1 \2, (int32_t)-16, (int32_t)16)",
    text,
)
path.write_text(text, encoding="utf-8")
PY

"${PYTHON}" - "${SOURCE_DIR}/common/socket.cpp" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = """#include <sys/ioctl.h>
#include <net/if.h>
#if !defined(__3DS__)
#include <ifaddrs.h>
#endif"""
new = """#include <sys/ioctl.h>
#if !defined(__3DS__)
#include <net/if.h>
#include <ifaddrs.h>
#endif"""
if old not in text:
    raise SystemExit(f"missing socket include anchor in {path}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
PY

"${PYTHON}" - "${SOURCE_DIR}/common/socket.cpp" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

set_options = """void SocketSetOptions(int aSocket) {
    // set socket to non-blocking mode
    int flags = fcntl(aSocket, F_GETFL, 0);
    SOCKET_RESET_ERROR();
    int rc = fcntl(aSocket, F_SETFL, ((unsigned int)flags) | O_NONBLOCK);
    if (rc == -1) {
        LOG_ERROR("failed to set to non-blocking: %d", SOCKET_LAST_ERROR);
    }

    // set socket to keep-alive mode
    SOCKET_RESET_ERROR();
    int optval = 1;
    if(setsockopt(aSocket, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("failed to set to keep-alive mode: %d", SOCKET_LAST_ERROR);
    }

    // set socket to dont-linger
    SOCKET_RESET_ERROR();
    struct linger lingerStruct = { 1, 0 };
    if (setsockopt(aSocket, SOL_SOCKET, SO_LINGER, &lingerStruct, sizeof(lingerStruct)) < 0) {
        LOG_ERROR("failed to set to dont-linger mode: %d", SOCKET_LAST_ERROR);
    }
}"""

set_options_3ds = """void SocketSetOptions(int aSocket) {
    int flags = fcntl(aSocket, F_GETFL, 0);
    SOCKET_RESET_ERROR();
    int rc = fcntl(aSocket, F_SETFL, ((unsigned int)flags) | O_NONBLOCK);
    if (rc == -1) {
        LOG_ERROR("failed to set to non-blocking: %d", SOCKET_LAST_ERROR);
    }
}"""

limit_buffer = """void SocketLimitBuffer(int aSocket, int64_t* amount) {
    SOCKET_RESET_ERROR();
    int bufferLength = 0;
    if (ioctl(aSocket, FIONREAD, &bufferLength) == -1) {
        LOG_ERROR("failed to retrieve buffer size: %d", SOCKET_LAST_ERROR);
        return;
    }
    if (*amount > bufferLength) {
        *amount = bufferLength;
    }
}"""

limit_buffer_3ds = """void SocketLimitBuffer(int aSocket, int64_t* amount) {
    (void)aSocket;
    (void)amount;
}"""

if set_options not in text:
    raise SystemExit(f"missing SocketSetOptions anchor in {path}")
if limit_buffer not in text:
    raise SystemExit(f"missing SocketLimitBuffer anchor in {path}")

text = text.replace(set_options, "#ifdef __3DS__\n" + set_options_3ds + "\n#else\n" + set_options + "\n#endif", 1)
text = text.replace(limit_buffer, "#ifdef __3DS__\n" + limit_buffer_3ds + "\n#else\n" + limit_buffer + "\n#endif", 1)
path.write_text(text, encoding="utf-8")
PY

for marker in \
    'uint64_t coopnet_get_client_hash(void)' \
    'coopnet_new3ds_identity(filepath.c_str())'; do
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
    -D__3DS__
    -include "${ROOT_DIR}/tools/new3ds/coopnet_3ds_compat.h"
    -Wno-nonnull-compare
    -Wno-unused-function
    -I"${SOURCE_DIR}/common"
    -I"${LIBJUICE_ROOT}/include"
    -I"${SOURCE_DIR}/lib/include"
    -I"${CTRULIB}/include"
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
verify_arm_archive "${LIB_DIR}/libcoopnet.a" "libcoopnet.a"

printf '%s\n' "${COOPNET_COMMIT}" > "${BUILD_ROOT}/SOURCE_COMMIT.txt"
sha256sum "${LIB_DIR}/libcoopnet.a" | tee "${BUILD_ROOT}/SHA256SUMS.txt"
echo "Built New 3DS CoopNet: ${LIB_DIR}/libcoopnet.a"
