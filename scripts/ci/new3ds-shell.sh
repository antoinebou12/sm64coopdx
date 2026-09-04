#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

cd "$(ci_root_dir)"
ci_export_devkitarm
ci_clean_dep_files build/new3ds-shell

make -f Makefile.new3ds print-config
make -f Makefile.new3ds port-smoke -j2
make -f Makefile.new3ds -j2

test -s build/new3ds-shell/sm64coopdx-new3ds.elf
test -s build/new3ds-shell/sm64coopdx-new3ds.3dsx
test -s build/new3ds-shell/sm64coopdx-new3ds.smdh
test -s build/new3ds-shell/new3ds_shader.shbin
arm-none-eabi-size build/new3ds-shell/sm64coopdx-new3ds.elf

echo "New 3DS shell + platform smoke passed"
