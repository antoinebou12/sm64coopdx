# Nintendo Switch Port Roadmap

The goal is a maintained native Horizon OS build of current SM64CoopDX with normal online multiplayer, Switch LDN local wireless, local split-screen, hybrid local+network play, Lua/DynOS mods, and reproducible CI.

## Milestone 0: toolchain bootstrap

Status: in progress

Acceptance criteria:
- current devkitA64 compiler is visible in CI
- current libnx package version is printed in CI logs
- a tiny libnx program compiles and links
- CI packages a valid `.nro`, `.elf`, `.map`, and `.nacp`
- no ROM is required for the bootstrap job

## Milestone 1: full game Switch build

Acceptance criteria:
- add an opt-in `TARGET_NX=1` build path
- build current upstream game code with devkitA64
- SDL2 + GLES2 renderer
- SDL2 audio
- no desktop-only Discord/Mumble code on Switch
- game links to `.elf` and packages to `.nro`
- build keeps symbols and a linker map in CI

## Milestone 2: platform integration

Acceptance criteria:
- dedicated Switch platform layer for startup/shutdown, paths, user/profile, keyboard and lifecycle
- correct SD-card data directory
- config/save persistence
- clean application exit on current Horizon OS
- dock/undock, HOME, suspend and resume are handled without corrupting state

## Milestone 3: input and rendering stability

Acceptance criteria:
- handheld controls
- paired Joy-Con
- Pro Controller
- controller hotplug/reconnect
- rumble where supported
- stable 1280x720 handheld output
- docked output path
- 60 FPS target for one local player

## Milestone 4: Internet networking

Acceptance criteria:
- BSD socket backend works on Switch
- PC host to Switch client
- Switch host to PC client where supported by the protocol
- Switch to Switch over normal LAN/Internet
- disconnect/reconnect errors return to a safe UI state

## Milestone 5: CoopNet

Acceptance criteria:
- current CoopNet dependency builds for Switch from source or from a reproducible package
- no opaque stale static libraries copied from old branches
- joining and hosting behavior matches current desktop protocol expectations
- lobby UI works on Switch

## Milestone 6: LDN local wireless

Acceptance criteria:
- LDN is a separate transport backend
- host local-wireless room
- discover local-wireless rooms
- join/leave cleanly
- at least two physical Switch consoles remain synchronized
- mockable session logic is testable on desktop CI where possible

## Milestone 7: local-player architecture and split-screen

Acceptance criteria:
- one process can own 1-4 local player contexts
- each local player has independent controller, camera and HUD state
- 2-player horizontal/vertical layouts
- 3-player layout
- 4-player grid layout
- no forced joystick-count hacks
- one simulation step, multiple viewports

## Milestone 8: hybrid multiplayer

Acceptance criteria:
- local player count is negotiated independently of network connection count
- two local players on one Switch can join an online session
- two Switch consoles can combine local split-screen with LDN
- legacy one-local-player clients remain compatible where protocol rules allow

## Milestone 9: mods and polish

Acceptance criteria:
- Lua mods load from SD card
- DynOS content works from SD card
- mod synchronization is tested online
- split-screen-incompatible mods fail gracefully or are clearly identified
- crash logs include build SHA and toolchain/runtime information
- memory pressure is handled in applet mode
- full-memory application mode is documented as the preferred launch mode for large mods and split-screen

## CI layers

1. Existing desktop regression CI remains intact.
2. Switch bootstrap CI verifies devkitA64/libnx independently of game assets.
3. Full Switch compile CI is added once the game target links.
4. Desktop tests cover transport/local-player logic that does not require Horizon hardware.
5. Hardware release checklist covers boot, exit, sleep/wake, controllers, network, LDN and split-screen.

## Branch policy

- `switch-next` is the long-lived integration branch.
- Feature work is developed on `agent/switch-*` branches and merged through reviewable PRs.
- Upstream `dev` is synchronized regularly.
- Large historical branches are not merged wholesale.
