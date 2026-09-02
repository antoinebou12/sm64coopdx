#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="${DEVKITPRO}/devkitA64/bin:${DEVKITPRO}/tools/bin:${PATH}"

python3 tools/switch/tests/run_tests.py

mkdir -p build/switch-ci
{
  aarch64-none-elf-gcc --version | head -n 1
  dkp-pacman -Q libnx devkitA64 switch-sdl2 switch-mesa switch-curl switch-zlib
} | tee build/switch-ci/TOOLCHAIN.txt

make -f Makefile.switch-portlibs clean
make -f Makefile.switch-portlibs -j2 probe

make -f Makefile.switch-platform clean
make -f Makefile.switch-platform -j2 probe

make -f Makefile.switch-game switch-toolchain-check
mkdir -p build/switch-ci/overlays
python3 tools/switch/source_overlay.py pc_main src/pc/pc_main.c build/switch-ci/overlays/pc_main.c
python3 tools/switch/source_overlay.py platform src/pc/platform.c build/switch-ci/overlays/platform.c
python3 tools/switch/source_overlay.py controller_bind src/pc/controller/controller_bind_mapping.c build/switch-ci/overlays/controller_bind_mapping.c
python3 tools/switch/source_overlay.py djui_controls src/pc/djui/djui_panel_controls.c build/switch-ci/overlays/djui_panel_controls.c
python3 tools/switch/source_overlay.py loading src/pc/loading.c build/switch-ci/overlays/loading.c
python3 tools/switch/source_overlay.py network src/pc/network/network.c build/switch-ci/overlays/network.c
python3 tools/switch/source_overlay.py djui_host src/pc/djui/djui_panel_host.c build/switch-ci/overlays/djui_panel_host.c
python3 tools/switch/ci_source_check.py

make -f Makefile.switch-game -j2 \
  build/us_switch/src/pc/pc_main.o \
  build/us_switch/src/pc/platform.o \
  build/us_switch/src/pc/loading.o \
  build/us_switch/src/pc/controller/controller_bind_mapping.o \
  build/us_switch/src/pc/djui/djui_panel_controls.o \
  build/us_switch/src/pc/network/network.o \
  build/us_switch/src/pc/network/socket/socket.o \
  build/us_switch/src/pc/network/socket/socket_ldn.o \
  build/us_switch/src/pc/network/socket/socket_ldn_util.o \
  build/us_switch/src/pc/network/socket/socket_ldn_glue.o \
  build/us_switch/src/pc/djui/djui_panel_host.o \
  build/us_switch/src/pc/djui/djui_panel_host_message.o \
  build/us_switch/src/pc/djui/djui_panel_ldn_browser.o \
  build/us_switch/src/pc/djui/djui_panel_join.o \
  build/us_switch/src/pc/platform/switch/switch_crash_log.o \
  build/us_switch/src/pc/platform/switch/switch_packet_safety.o

for prefix in \
  build/switch-portlibs/sm64coopdx-switch-portlibs \
  build/switch-platform/sm64coopdx-switch-platform; do
  test -s "${prefix}.nro"
  test -s "${prefix}.elf"
  test "$(dd if="${prefix}.nro" bs=1 skip=16 count=4 2>/dev/null)" = "NRO0"
done

echo "Switch portability gates passed"
