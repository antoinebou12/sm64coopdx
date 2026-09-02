#!/usr/bin/env bash
set -euo pipefail

MAKEROM_VERSION="v0.17"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DIR="${ROOT_DIR}/build/new3ds-tools/bin"
MAKEROM="${BIN_DIR}/makerom"

if [[ -x "${MAKEROM}" ]]; then
    exit 0
fi

mkdir -p "${BIN_DIR}"

case "$(uname -s)" in
    Linux*)  ARCHIVE="makerom-${MAKEROM_VERSION}-ubuntu_x86_64.zip" ;;
    MINGW*|MSYS*|CYGWIN*) ARCHIVE="makerom-${MAKEROM_VERSION}-win_x86_64.zip" ;;
    Darwin*) ARCHIVE="makerom-${MAKEROM_VERSION}-osx_x86_64.zip" ;;
    *) echo "Unsupported host for makerom fetch: $(uname -s)" >&2; exit 1 ;;
esac

URL="https://github.com/3DSGuy/Project_CTR/releases/download/makerom-${MAKEROM_VERSION}/${ARCHIVE}"
TMP_ZIP="$(mktemp "${TMPDIR:-/tmp}/makerom.XXXXXX.zip")"
trap 'rm -f "${TMP_ZIP}"' EXIT

curl --fail --location --retry 3 --output "${TMP_ZIP}" "${URL}"
unzip -j -o "${TMP_ZIP}" -d "${BIN_DIR}"
chmod +x "${MAKEROM}" 2>/dev/null || true

if [[ -f "${BIN_DIR}/makerom.exe" && ! -f "${MAKEROM}" ]]; then
    cp "${BIN_DIR}/makerom.exe" "${MAKEROM}"
fi

if [[ ! -x "${MAKEROM}" ]]; then
    echo "makerom was not installed to ${MAKEROM}" >&2
    exit 1
fi

echo "Installed makerom: ${MAKEROM}"
