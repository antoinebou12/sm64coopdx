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

The source and build integration are committed, but GitHub currently reports no Actions workflow runs for this repository/branch, and the active development environment does not have a local devkitARM toolchain available. Therefore:

- the platform shell/build graph is implemented but not currently CI-verified in this session;
- the Citro3D renderer has not yet been compiler-verified by devkitARM;
- the full CoopDX New 3DS ELF has not yet completed a verified link;
- no current claim is made that gameplay renders correctly on hardware.

The next hard gate is **a green devkitARM compile/link followed by a first hardware game frame**.

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
make -f Makefile.new3ds-game new3ds-3dsx -j2
```

Expected outputs:

```text
build/us_new3ds/sm64coopdx.elf
build/us_new3ds/sm64coopdx.smdh
build/us_new3ds/sm64coopdx.3dsx
```

The full-game target currently forces `NEW3DS_COOPNET=0`. CoopNet should not be enabled until the base engine, renderer, audio, SD paths, and local socket transport are stable.

The repository's existing legal asset requirements still apply. No Nintendo ROM or extracted proprietary assets are added to CI/release artifacts by this port.

## UX direction

The New 3DS port must not shrink desktop DJUI onto a 320x240 panel.

- **Top screen = gameplay.** The 400x240 display is reserved for the world, short notifications, and essential connection state.
- **Bottom screen = interaction.** Host/join, player list, chat entry, settings, diagnostics, and recovery actions belong here.
- **Large touch targets.** Every touch action must remain usable with physical controls.
- **A/touch = primary action. B = back.** Navigation should be predictable across every page.
- **C-Stick = camera.** Avoid requiring touch for normal gameplay.
- **No silent networking states.** Connecting, authorization, timeout, retry, and failure must be visible.
- **No fake readiness.** Unimplemented actions remain labeled/disabled until their backend exists.
- **Readable at native resolution.** Avoid desktop-density sidebars, tiny controls, hover interactions, and long modal paragraphs.

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
