# SM64CoopDX New 3DS port

This directory is a **separate New Nintendo 3DS target in the same repository**. It must not make the desktop or Nintendo Switch builds depend on libctru, Citro2D, or Citro3D.

## Current status

The first vertical slice is intentionally small but real:

- native devkitARM/libctru `.3dsx` build;
- New 3DS hardware detection with the New 3DS speedup path enabled when available;
- Citro2D/Citro3D initialization and clean shutdown;
- top-screen status view at 400x240;
- bottom-screen touch-first navigation at 320x240;
- D-pad, A, B, L/R, START and touch input;
- CI build and downloadable `.3dsx`, `.smdh`, and `.elf` artifacts.

It does **not** claim that gameplay is ready. The UI marks the renderer, game loop, and networking as pending until those components are actually connected.

## Build

Install devkitPro's 3DS toolchain and libraries (`3ds-dev`), then run:

```sh
make -f Makefile.new3ds -j2
```

Artifacts are written under:

```text
build/new3ds-shell/sm64coopdx-new3ds.3dsx
build/new3ds-shell/sm64coopdx-new3ds.smdh
build/new3ds-shell/sm64coopdx-new3ds.elf
```

The New 3DS build graph is intentionally independent from `Makefile`, `Makefile.switch`, and `Makefile.switch-game`.

## UX direction

The New 3DS port should not shrink the desktop DJUI onto a 320x240 screen. The handheld UI follows these rules:

- **Top screen = game and primary status.** Keep gameplay visually clean and use the 400x240 display for the world, important connection state, and short notifications.
- **Bottom screen = interaction.** Host/join, players, settings, diagnostics, keyboard entry, and touch actions live here.
- **Large touch targets.** Menu rows are sized for fingers and also remain fully navigable by D-pad/buttons.
- **One obvious primary action.** A/touch opens the focused item; B always goes back; START exits only at the platform-shell stage and later becomes a pause/menu shortcut.
- **No silent network state.** Joining, authorization, timeouts, reconnects, and errors must expose a visible state and a recovery action.
- **No fake readiness.** Disabled or unfinished functionality is labeled as such instead of leading to dead screens.
- **Readable at native resolution.** Avoid desktop-density sidebars, tiny icon-only controls, hover-only affordances, and long paragraphs.

## Target architecture

Keep 3DS-only code under `src/pc/platform/new3ds/` and gate shared changes with `__3DS__` only where an interface genuinely needs it.

### 1. Renderer

Port the proven Nintendo 3DS SM64 Citro3D backend to the current CoopDX `GfxRenderingAPI` rather than trying to use desktop OpenGL/SDL on the handheld.

Required adaptation points:

- current CoopDX shader creation takes `struct ColorCombiner *`, not the older packed shader-id API;
- implement the current `end_frame`, `finish_render`, `get_name`, and `shutdown` hooks;
- cap texture/cache growth to a realistic New 3DS memory budget;
- start with stereoscopic 3D disabled and add it only after the mono path is stable;
- use the top-left render target for gameplay; reserve the bottom render target for the handheld UI.

### 2. Window/lifecycle layer

The current desktop window-manager header includes SDL. For 3DS, introduce a native implementation that uses:

- `aptMainLoop()` for lifecycle;
- fixed 400x240 game dimensions;
- `osGetTime()` for timing;
- `svcSleepThread()` for delays;
- no fullscreen/window-position concepts;
- software keyboard support through libctru for text fields when the game UI is integrated.

Do not add SDL as a dependency to the New 3DS target merely to satisfy desktop window types.

### 3. Input

Map native HID directly:

- Circle Pad -> movement;
- C-Stick -> camera on New 3DS;
- A/B/X/Y -> game actions;
- L/R/ZL/ZR -> camera/secondary actions;
- D-pad -> menu navigation / optional binds;
- touch -> bottom-screen UI only by default.

Input polling must remain independent from rendering so networking or a slow menu cannot stall controls.

### 4. Audio

Use an `ndsp` backend instead of SDL audio. Start with a small fixed ring of linear-memory buffers and the game's existing PCM callback. Keep resampling and channel count conservative until underrun telemetry is clean.

### 5. Files and ROM-derived assets

Use a dedicated data root, for example:

```text
sdmc:/3ds/sm64coopdx/
```

Do not ship Nintendo ROM data or extracted copyrighted game assets in CI artifacts. The build/release flow should preserve the project's existing asset requirements.

### 6. Networking

Bring networking up in layers:

1. libctru SOC initialization and clean teardown;
2. raw LAN socket smoke test;
3. CoopDX socket abstraction;
4. private host/join flow;
5. CoopNet signaling/ICE only after the base transport is stable;
6. explicit user-facing states for discovery, connecting, authorization, retry, failure, and success.

The bottom screen should show connection progress and recovery actions while gameplay remains on the top screen.

## Performance policy

New 3DS is the primary target. Old 3DS is not a performance target for this port.

Initial budgets:

- 30 FPS stability before pursuing 60 FPS;
- 400x240 mono rendering first;
- avoid per-frame heap allocation in renderer/input/audio hot paths;
- bounded texture/shader caches;
- avoid CPU/GPU synchronization except at deliberate frame boundaries;
- gather frame time, texture memory, audio underruns, and network queue depth in the diagnostics screen before optimizing blindly.

## Integration sequence

1. **Foundation (done in this branch):** native `.3dsx`, dual-screen UI, input, hardware detection, CI.
2. **Renderer:** adapt Citro3D backend to current CoopDX rendering API and render a game frame on the top screen.
3. **Game loop + files:** connect the existing main loop, ROM-derived asset loading, timing, and lifecycle.
4. **Input + audio:** native controller and NDSP backends.
5. **Local multiplayer:** SOC/socket backend and LAN host/join.
6. **CoopNet:** signaling/ICE compatibility plus clear admission/error UX.
7. **Polish:** bottom-screen player list, software keyboard, quick settings, diagnostics overlay, suspend/resume testing, memory/performance tuning.
8. **Packaging:** versioned 3DSX release artifact; add CIA only if the project chooses to support installed-title packaging.

## Definition of playable

Do not label the target playable until CI builds successfully and hardware testing confirms all of the following:

- boots to gameplay on New 3DS without manual debug patches;
- stable rendering for a representative level;
- controller + C-Stick camera usable;
- audio plays without persistent underruns;
- save/config paths work on SD;
- suspend/resume and HOME return safely;
- local host/join succeeds repeatedly;
- failures surface a visible reason instead of hanging;
- no obvious unbounded memory growth during a 30-minute session.
