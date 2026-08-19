# Switch Toolchain Policy

## Canonical build dependencies

The Switch port builds against devkitPro's Nintendo Switch toolchain. Atmosphere is a runtime compatibility target, not a link dependency.

As of 2026-08-18:

- devkitPro `switch-dev` is the canonical package group.
- devkitPro's current libnx package recipe is `libnx 4.12.0`.
- Atmosphere `1.11.2` is the current runtime reference and adds basic support for Horizon OS `22.5.0`.
- Atmosphere 1.11.2 moved to the current GCC 16/newlib devkitA64 generation.

The exact compiler and package versions used by CI must be printed into every workflow log. Release builds will also record them in build metadata.

## CI image

Bootstrap CI currently uses:

```text
devkitpro/devkita64:latest
```

This is intentional during initial bring-up so the port is continuously exposed to the current devkitPro toolchain. Before the first public Switch release, the image must be pinned to an immutable digest or dated tag and updated deliberately.

## Local installation

A normal devkitPro installation should include the Switch development group and the portlibs needed by the full game. The bootstrap smoke test only requires devkitA64, libnx and devkitPro's Switch packaging tools.

The full game target is expected to require at least:

- libnx
- SDL2 for Switch
- EGL/GLES2 Switch portlibs
- curl
- zlib

Additional dependencies must be added only when the current upstream feature actually requires them.

## Build commands

Bootstrap smoke test:

```sh
make -f Makefile.switch toolchain-check
make -f Makefile.switch smoke
```

Expected artifacts:

```text
build/switch-smoke/sm64coopdx-switch-smoke.nro
build/switch-smoke/sm64coopdx-switch-smoke.elf
build/switch-smoke/sm64coopdx-switch-smoke.map
build/switch-smoke/sm64coopdx-switch-smoke.nacp
```

The bootstrap target deliberately does not use a Mario 64 ROM and does not build game assets. It exists only to prove the compiler, libnx link and NRO packaging path before the real game target is introduced.

## Runtime compatibility

The supported runtime matrix will be documented separately from the compiler matrix. A build can compile successfully against libnx and still fail due to Horizon or homebrew-loader lifecycle changes.

Current runtime bring-up must explicitly test:

- clean process exit
- HOME button behavior
- sleep and wake
- dock and undock
- controller reconnect
- network loss and recovery

Horizon OS 22.x lifecycle changes make clean shutdown behavior a port requirement rather than optional polish.
