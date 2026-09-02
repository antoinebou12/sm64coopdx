#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
export DEVKITPRO DEVKITARM
export PATH="${DEVKITPRO}/devkitARM/bin:${DEVKITPRO}/tools/bin:${PATH}"

# Host- or container-generated dependency files can pin absolute /work paths that
# do not match every local CI runner mount layout (for example act on Windows).
find build/new3ds-shell -name '*.d' -delete 2>/dev/null || true

make -f Makefile.new3ds print-config
make -f Makefile.new3ds port-smoke -j2
make -f Makefile.new3ds -j2

test -s build/new3ds-shell/sm64coopdx-new3ds.elf
test -s build/new3ds-shell/sm64coopdx-new3ds.3dsx
test -s build/new3ds-shell/sm64coopdx-new3ds.smdh
test -s build/new3ds-shell/new3ds_shader.shbin
arm-none-eabi-size build/new3ds-shell/sm64coopdx-new3ds.elf

echo "New 3DS shell + platform smoke passed"
