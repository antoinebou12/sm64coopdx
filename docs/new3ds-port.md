# SM64CoopDX New 3DS port

This is a **separate New Nintendo 3DS target in the same repository**. Desktop and Nintendo Switch builds must not gain libctru, Citro2D, Citro3D, NDSP, or devkitARM dependencies from this work.

## Current status

The branch now contains two New 3DS build paths:

1. **Platform shell** (`Makefile.new3ds`) — small dual-screen hardware bring-up application and compile-smoke suite.
2. **Full-game integration** (`Makefile.new3ds-game`) — isolated CoopDX engine target using the native 3DS renderer, lifecycle, controller, audio, threading, and filesystem/platform layers.

Implemented code currently includes:

- devkitARM/libctru New 3DS target separation;
- New 3DS hardware detection and `osSetSpeedupEnable(true)` path;
- 400x240 top screen and 320x240 bottom-screen handheld UX foundation;
- touch, D-pad, A/B/X/Y, L/R/ZL/ZR, START, Circle Pad, and C-Stick polling;
- native CoopDX `ControllerAPI` backend: Circle Pad movement + C-Stick camera/C-buttons;
- native non-SDL `gfx_wm_*` lifecycle/timing implementation;
- Citro3D/PICA200 implementation of the current CoopDX `GfxRenderingAPI`;
- current `ColorCombiner` translation to PICA200 TEV stages for common SM64 paths;
- two independent texture-coordinate sets, depth, scissor, blending, texture swizzling, filtering/wrapping, texture-edge alpha, and fog pass;
- explicit degraded-combiner diagnostics for effects PICA200 cannot faithfully represent in the initial backend;
- native NDSP stereo PCM16 audio backend with bounded linear DMA buffers;
- native libctru `Thread` + `LightLock` implementation of the CoopDX thread abstraction;
- SD-card platform paths under `sdmc:/3ds/sm64coopdx`;
- no-op console replacements for desktop mouse, Mumble, and update-checker services;
- reproducible devkitARM Lua 5.3.5 dependency build;
- PICA shader generation/embedding;
- platform compile-smoke targets for renderer, controller, lifecycle, audio, threading, and shader assembly;
- a separate full-game `.elf` / `.3dsx` packaging target.

### Verification state

**Do not call the port playable yet.**

The platform shell, integration compile smoke, and Citro3D renderer now compile under devkitARM. The full `.3dsx` link is in progress locally and in CI; gameplay on hardware is still unverified.

### Docker and local CI

Switch and New 3DS builds are isolated on purpose. They write to different `build/` subtrees and use separate compose files, so both can run in parallel:

```sh
docker compose -f docker-compose.switch.yml run --rm switch-portability
docker compose -f docker-compose.new3ds.yml run --rm new3ds-shell
docker compose -f docker-compose.new3ds.yml run --rm new3ds-integration
```

Full baserom-backed console artifacts require a private `SM64_BASEROM_US_URL` environment variable or repository secret, matching the Switch workflow.

To exercise the GitHub workflows locally with [act](https://github.com/nektos/act):

```sh
act -W .github/workflows/build-new3ds.yml -j build-new3ds-shell
act -W .github/workflows/build-switch-game.yml -j portability
```

The repository ships a root `.actrc` with container defaults for act.

## Build targets

### Platform shell + platform compile smoke

Install devkitPro's `3ds-dev` group, then run:

```sh
make -f Makefile.new3ds port-smoke -j2
make -f Makefile.new3ds -j2
```

Expected shell artifacts:

```text
build/new3ds-shell/sm64coopdx-new3ds.3dsx
build/new3ds-shell/sm64coopdx-new3ds.smdh
build/new3ds-shell/sm64coopdx-new3ds.elf
```

`port-smoke` additionally cross-compiles the native renderer/controller/window/audio/thread layers and assembles the PICA shader without requiring a full engine link.

### Full CoopDX target

```sh
make -f Makefile.new3ds-game new3ds-integration-smoke -j2
make -f Makefile.new3ds-game new3ds-3dsx -j2
```

`new3ds-integration-smoke` compiles shared engine/DynOS/platform objects without requiring a baserom-backed link. Use it for fast CI and local portability checks.

Expected outputs:

```text
build/us_new3ds/sm64coopdx.elf
build/us_new3ds/sm64coopdx.smdh
build/us_new3ds/sm64coopdx.3dsx
```

### Console distribution (Switch NRO + New 3DS 3DSX/CIA)

ROM-free release folders and ZIPs are written under `dist/`:

```sh
make -f Makefile.console-dist console-dist -j2
```

This produces:

```text
dist/sm64coopdx-switch-1.5.1-switch/
dist/sm64coopdx-new3ds-1.5.1-new3ds/3ds/sm64coopdx/sm64coopdx.{3dsx,cia}
dist/sm64coopdx-console-1.5.1-console/   # combined NRO + 3DSX + CIA
```

`new3ds-cia` downloads `makerom` on first use into `build/new3ds-tools/bin/`.

### Real hardware install (Homebrew Launcher)

**Always overwrite both app files** from a fresh `new3ds-dist` (stale `.3dsx` causes old hash/freeze behavior):

```text
sdmc:/3ds/sm64coopdx/sm64coopdx.3dsx   # overwrite
sdmc:/3ds/sm64coopdx/sm64coopdx.smdh   # overwrite (same basename)
sdmc:/3ds/sm64coopdx/baserom.us.z64   # vanilla US .z64 only, 8 MiB
```

After installing a new build, **delete** any stale hash cache so the new binary re-checks cleanly:

```text
sdmc:/3ds/sm64coopdx/baserom.us.z64.md5   # delete if present
```

`baserom.us.z64` must be a **vanilla US** Super Mario 64 dump in native `.z64` byte order:

- Required MD5: `20b854b239203baf6c961b850a4a51a2`
- EU / JP / Shindou dumps are rejected (wrong region)
- `.v64` / `.n64` byte-swapped US dumps are auto-converted once to `.z64` on boot
- A patched or wrong-size file fails with a bottom-screen error and START/HOME to exit (not a silent hang)
- After a hash failure, `runtime.log` must show separate lines `expected=` and `computed=` (if you still see a single line `invalid baserom.us.z64 hash path=...` plus `loading: ui init`, the SD still has an old `.3dsx`)

Optional after first successful boot (speeds later boots):

```text
sdmc:/3ds/sm64coopdx/baserom.us.z64.md5
sdmc:/3ds/sm64coopdx/logs/runtime.log
```

On launch, the **bottom screen** should show boot phases within 1–2 seconds (`Loading config…`, `Checking ROM…`, etc.). If Homebrew Launcher keeps spinning with a blank bottom screen for minutes, the build is too old or the `.3dsx`/`.smdh` pair is missing. If the bottom screen shows a ROM MD5/region error, replace the SD file with a verified US `.z64` (Azahar may be using a different host copy than the SD card).

### Dual-screen UX (full game)

- **Top (400×240):** gameplay + DJUI menus (compact logo/buttons, scrollable panel bodies via C-Stick Y / L+R)
- **Bottom (320×240):** boot/runtime log viewer via PrintConsole (readable text; not a custom BGR8 blit)
- **Touch:** maps to the top DJUI cursor (320→400 X scale)
- **Default LAN port:** `1234` (host and join); use the on-screen DJUI keypad for port, IP, and password (native `swkbd` is disabled — it crashes with the bottom PrintConsole)
- **Skip intro cutscene:** on by default (Host Settings still toggles it)
- **Camera:** C-Stick drives free-cam / analog look when enabled; in menus C-Stick Y scrolls overflowing DJUI lists
- PC and Switch builds are unchanged (code is `__3DS__` / Makefile-excluded)

The full-game target defaults to `NEW3DS_COOPNET=1` for Android `sm64coop-android` lobbies. SOC init stays deferred until networking. Use `NEW3DS_COOPNET=0` for a smaller offline-first binary.

```sh
make -f Makefile.new3ds-game NEW3DS_COOPNET=1 new3ds-integration-smoke -j2
make -f Makefile.new3ds-game NEW3DS_COOPNET=1 new3ds-3dsx -j2
```

The repository's existing legal asset requirements still apply. No Nintendo ROM or extracted proprietary assets are added to CI/release artifacts by this port.

## Logging and diagnostics

### Build-time log floor

`Makefile.new3ds-game` accepts `NEW3DS_LOG_LEVEL` (`0`–`3`):

| Level | Meaning |
|-------|---------|
| `0` | Off |
| `1` | Errors |
| `2` | Info (default) |
| `3` | Verbose |

### Runtime category flags (`sm64config.txt`)

| Key | Default | Purpose |
|-----|---------|---------|
| `new3ds_log_net` | `false` (also follows `debug_info`) | Socket/DNS/host checkpoints |
| `new3ds_log_gfx` | `false` | Citro3D init/degraded combiner messages |
| `new3ds_log_perf` | `false` (also follows `show_fps`) | Rolling gfx stats every ~5 s |
| `new3ds_log_coopnet` | `false` | CoopNet console ring-buffer mirror |

Logs go to Citra stdout, `svcOutputDebugString`, and a 32-line in-memory ring buffer surfaced on the shell **MULTIPLAYER** and **DIAGNOSTICS** pages.

### CoopNet SD logs

When `NEW3DS_COOPNET=1`, CoopNet always appends to:

```text
sdmc:/3ds/sm64coopdx/logs/coopnet.log
sdmc:/3ds/sm64coopdx/logs/coopnet_checkpoint.txt
```

## Performance defaults

On `__3DS__` the game defaults to:

- `configFrameLimit = 30`
- `configFramerateMode = manual`
- `configAmountOfPlayers = 4`

The Citro3D backend exposes `new3ds_gfx_get_stats()` with per-frame triangle/vertex counts, texture pool use, VBO fill %, degraded/dropped draw totals, and a 60-frame rolling average frame time. Texture pool size is capped at **768** entries for RAM headroom.

## LAN hosting

The host panel shows `Clients join at <ip>:<port>` when SOC is ready. Hosting is blocked with an explicit message when `socInit` failed. Socket creation is guarded so networking code never calls `socket()` before SOC is available.

PC clients should use **Direct Join** with the IPv4 shown on the 3DS host screen.

## CoopNet (opt-in)

Build with `NEW3DS_COOPNET=1` to link the ARM11 libjuice + CoopNet static libraries built by:

```sh
bash tools/new3ds/build-libjuice.sh
bash tools/new3ds/build-coopnet.sh
```

CoopNet uses game name `sm64coop-android` for lobby/server compatibility (same as Switch). The 3DS build is **client-only** (`NO_SERVER` libjuice). CI exposes a manual `build-new3ds-coopnet` workflow for compile-only validation.

**Identity:** the client fingerprints `sdmc:/3ds/sm64coopdx/sm64coopdx.3dsx` (same murmur64a approach as the Switch NRO). Install both the `.3dsx` and `.smdh` under that path; a missing or unreadable binary yields a zero fingerprint and public join is blocked with an explicit reinstall message.

**Lobbies UI (400×240):** Public/Private lists omit the desktop side description panel (~410px). Descriptions show in-panel when a lobby is highlighted; page size is 4; Back/Refresh are compact. Host → Network System → CoopNet and private passwords use the in-game DJUI keypad (same approach as Switch — native `swkbd` is off by default because it conflicts with the bottom-screen log console).

**Menus:** Options includes a top-level **CoopNet** entry (Public / Private / Host CoopNet Lobby). Join labels CoopNet Public/Private separately from Direct Connection.

## UX direction

The New 3DS port must not shrink desktop DJUI onto a 320x240 panel.

- **Top screen = gameplay + menus.** The 400x240 display holds the world and compact, scrollable DJUI panels (Host/Join/Options/Quit).
- **Bottom screen = logs.** PrintConsole boot/runtime/CoopNet diagnostics only (no interactive DJUI on bottom).
- **Large touch targets.** Every touch action must remain usable with physical controls.
- **A/touch = primary action. B = back.** Navigation should be predictable across every page.
- **C-Stick = camera in-game; scroll in menus.** Avoid requiring touch for normal gameplay.
- **No silent networking states.** Connecting, authorization, timeout, retry, and failure must be visible.
- **No fake readiness.** Unimplemented actions remain labeled/disabled until their backend exists.
- **Readable at native resolution.** Avoid desktop-density sidebars, tiny controls, and long modal paragraphs.

### Intended in-game bottom-screen structure

```text
HOME
├─ Players
├─ Multiplayer
│  ├─ Host
│  ├─ Join LAN
│  └─ CoopNet
├─ Chat
├─ Quick Settings
└─ Diagnostics
```

During normal gameplay, the bottom screen should default to a lightweight player/network status page rather than constantly rendering the full settings UI.

## Renderer architecture

The port uses Citro3D/PICA200 directly instead of OpenGL or SDL.

### Current renderer strategy

CoopDX generates a CPU vertex stream and describes color-combiner behavior through `struct ColorCombiner`. The New 3DS backend translates the common cases into PICA200 texture-environment stages.

The compact GPU vertex layout currently carries:

- clip-space position: 4 floats;
- texture coordinates 0: 2 floats;
- texture coordinates 1 / light-map slot: 2 floats;
- primary varying color: 4 floats.

PICA200 has a much more constrained fragment pipeline than desktop GLSL. The implementation therefore supports the common SM64 combiner path directly and records degraded draws for unsupported extended behavior instead of silently producing a false-fidelity result.

Initial known degradation candidates include:

- multiple unrelated per-vertex color inputs in one combiner;
- shader noise;
- desktop-style post effects;
- advanced world-geometry/light-map combinations that exceed the initial TEV mapping.

These counters should be surfaced on the bottom-screen diagnostics page during hardware validation.

### Renderer performance policy

- stable **30 FPS** before attempting 60 FPS;
- 400x240 mono first;
- stereoscopic 3D stays off until the mono renderer is correct and has headroom;
- bounded shader/texture pools;
- no per-frame heap allocation in the triangle hot path;
- avoid CPU/GPU sync except at deliberate frame boundaries;
- measure degraded combiners, dropped draws, VBO use, texture memory, frame time, and C3D processing time before optimizing.

## Native platform layers

### Lifecycle/window

The New 3DS implementation uses:

- `aptMainLoop()`;
- fixed 400x240 game dimensions;
- `osGetTime()`;
- `svcSleepThread()`;
- libctru HOME/sleep behavior;
- no desktop window/fullscreen/position concepts.

Citro3D owns frame begin/end; the window layer owns application lifecycle and input polling.

### Input

Native HID mapping:

- Circle Pad -> Mario movement;
- C-Stick -> extended camera stick and C-button directions;
- A/B/X/Y -> game actions;
- L/R/ZL/ZR -> shoulder/secondary bindings;
- D-pad -> game/menu navigation;
- touch -> bottom-screen UI.

The runtime samples HID once per game iteration and shares that snapshot with the controller and UI to avoid consuming edge-triggered inputs twice.

### Audio

The NDSP backend currently uses:

- stereo PCM16;
- 32 kHz output;
- four bounded linear-memory wave buffers;
- cache flush before queueing;
- explicit shutdown/cleanup.

Hardware validation must check persistent underruns and suspend/resume behavior.

### Threads

The 3DS target maps CoopDX `ThreadHandle` to libctru `Thread` and `LightLock`.

Forced pthread-style cancellation is intentionally unsupported. Worker threads must terminate through their normal stop conditions and then be joined.

### Files and ROM-derived assets

Primary root:

```text
sdmc:/3ds/sm64coopdx/
```

Expected runtime content will eventually include the same user-provided/legal ROM-derived data, config, saves, mods supported by the subset of CoopDX that proves viable on New 3DS.

## Networking plan

Networking remains after the first verified game frame.

Bring-up order:

1. initialize/teardown libctru SOC cleanly;
2. UDP/TCP loopback/LAN smoke test;
3. connect CoopDX socket abstraction;
4. repeated private LAN host/join tests;
5. connection-state UI on the bottom screen;
6. port/build `libjuice` and CoopNet dependencies only after the base transport is stable;
7. CoopNet signaling/ICE;
8. explicit public-lobby authorization/error states.

Do not repeat the silent-admission problem seen on other console work: every network wait must have visible progress, timeout, reason, and retry/back actions.

## Milestones

### M0 — Native shell

**Implemented; compile/hardware verification still required in the current environment.**

- dual-screen application shell;
- New 3DS hardware check;
- touch/physical menu navigation;
- CI workflow and artifacts definition.

### M1 — Native CoopDX platform layer

**Implemented in source; verification pending.**

- Citro3D renderer;
- native lifecycle/window manager;
- native controller;
- NDSP audio;
- libctru threads;
- SD-card platform paths;
- platform compile-smoke suite.

### M2 — First full-game ELF / first Mario frame

**Current priority.**

- make `Makefile.new3ds-game` compile and link with devkitARM;
- fix remaining desktop assumptions found by the compiler;
- generate a `.3dsx`;
- boot on New 3DS;
- load legal user-provided ROM-derived assets;
- render a representative level;
- verify Circle Pad + C-Stick;
- verify NDSP audio.

### M3 — Stability and handheld UX integration

- integrate bottom-screen menu into gameplay rather than only the shell;
- software keyboard for text/chat/IP entry;
- quick settings;
- player list;
- renderer/audio/memory diagnostics;
- HOME/suspend/resume tests;
- 30-minute memory-stability run.

### M4 — LAN multiplayer

- SOC socket backend;
- host/join;
- disconnect/reconnect UX;
- repeated two-device test matrix.

### M5 — CoopNet

- cross-build dependencies;
- signaling/ICE;
- private lobby validation;
- public-lobby identity/authorization behavior;
- clear error/retry UI.

### M6 — Optimization and optional features

- renderer hot-path profiling;
- texture/cache tuning;
- Lua/mod memory policy;
- optional 60 FPS experiments;
- optional stereoscopic 3D only if performance and correctness permit;
- release packaging/polish.

## Definition of playable

Do not label the target playable until a verified build and New 3DS hardware test confirm all of the following:

- boots directly into the game without manual debugger patches;
- representative levels render correctly enough for normal play;
- Circle Pad movement and C-Stick camera are usable;
- audio runs without persistent underruns;
- config/save paths persist on SD;
- HOME/suspend/resume and clean exit are safe;
- no obvious unbounded memory growth during a 30-minute session;
- local host/join succeeds repeatedly;
- networking failures provide a visible reason/recovery action.

CoopNet availability is a later milestone and is not required to call the initial offline/LAN target technically playable, but it is required for feature parity with the intended comprehensive port.
