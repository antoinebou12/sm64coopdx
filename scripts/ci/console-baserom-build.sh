#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

cd "$(ci_root_dir)"

platform="${1:-}"
case "${platform}" in
    new3ds)
        ci_export_devkitarm
        ci_fetch_us_baserom
        make -f Makefile.new3ds-game new3ds-3dsx -j2
        ;;
    switch)
        ci_export_devkita64
        ci_fetch_us_baserom
        make -f Makefile.switch-game switch-nro -j2
        ;;
    *)
        echo "usage: $0 <new3ds|switch>" >&2
        exit 1
        ;;
esac

ci_remove_us_baserom
