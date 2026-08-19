# Nintendo Switch port

The Switch build targets Horizon OS with devkitA64/libnx, devkitPro SDL2 and GLES2. Desktop builds remain on SDL3.

## SD card layout

Use title override/full application mode when possible. The port creates its data directory automatically.

```text
sdmc:/switch/sm64coopdx/
├── sm64coopdx.nro
└── baserom.us.z64
```

The ROM must be a legally obtained North American Super Mario 64 ROM matching `sm64.us.sha1`. The ROM is not part of this repository.

## Local build

With devkitPro and Switch portlibs installed:

```sh
export DEVKITPRO=/opt/devkitpro
make -f Makefile.switch-game switch-nro -j2
```

Output is written below `build/us_switch/`.

## CI

`.github/workflows/build-switch-game.yml` has two gates.

1. **Horizon portability gates**, always run without a ROM. They build real libnx NRO probes for the SDL2/GLES2/curl/zlib stack and platform/native-input layer, generate the Switch source overlays, and validate Switch-specific source invariants.
2. **Full Horizon game NRO**, runs on pushes/manual dispatches only when repository secret `SM64_BASEROM_US_URL` is configured. The secret must be a private HTTPS URL for the owner's legally obtained US baserom. CI verifies its SHA-1, builds and validates the full NRO, removes the ROM, and retains only build metadata/hashes rather than the baserom-backed binaries.

## Hardware validation checklist

After CI is green, validate on real hardware because GitHub Actions cannot exercise Horizon GPU, applet, audio or HID runtime behavior:

- boot using title override and reach the first rendered frame;
- boot without a ROM and verify the SD-card placement message appears instead of a crash;
- launch in handheld and docked modes;
- test paired Joy-Con, Pro Controller and reconnects;
- test rumble and verify it stops cleanly;
- press HOME, suspend/resume, and verify graphics/audio/input recover;
- host and join a direct socket session over Wi-Fi;
- test a normal Lua mod and a larger mod for memory/SD-card behavior;
- exit through HOME/application shutdown, then relaunch immediately;
- run for at least 20–30 minutes and watch for audio queue growth, rendering corruption or network stalls.

## Known areas that still need hardware work

- Direct socket networking currently starts with an IPv6 dual-stack UDP socket. An IPv4 fallback may be needed on networks where IPv6 socket creation is unavailable.
- Text chat still follows the SDL keyboard/text-input path. A native Horizon software keyboard (`swkbd`) integration would improve controller-only use.
- Four libnx controller slots are available at the input boundary, but local split-screen/game-state integration is not enabled yet.
- There is no Switch-native crash/backtrace screen yet. A future debug build should save a compact crash log to the SD card and/or support nxlink logging.
- Applet mode has less usable memory than full application mode. The port warns about this, but larger mods should be tested with title override.
