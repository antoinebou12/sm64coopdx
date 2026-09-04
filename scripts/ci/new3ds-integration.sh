#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

cd "$(ci_root_dir)"
ci_export_devkitarm
ci_clean_dep_files build/us_new3ds

make -f Makefile.new3ds-game new3ds-integration-smoke -j2

echo "New 3DS integration compile smoke passed"
