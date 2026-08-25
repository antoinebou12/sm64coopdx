# Nintendo Switch build and packaging

The Switch build targets Horizon OS with devkitA64/libnx, devkitPro SDL2 and GLES2. Desktop builds remain on SDL3. For end-user setup, see the [Switch installation guide](SWITCH_INSTALL.md).

## SD card layout

Use title override/full application mode. The port creates its logs directory automatically.

```text
sdmc:/switch/sm64coopdx/
├── sm64coopdx.nro
├── baserom.us.z64
├── icon.jpg
├── lang/
└── logs/
```

The ROM must be a legally obtained North American Super Mario 64 ROM matching `sm64.us.sha1`. The ROM is not part of this repository.

## Local build

The repository includes Docker-based dependency scripts for Lua, libjuice, and CoopNet. From Linux/macOS with Docker and a private `baserom.us.z64` in the repository root:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64:latest bash -lc \
  'source /opt/devkitpro/switchvars.sh && make -f Makefile.switch-sdmc SWITCH_COOPNET=1 switch-nro -j1'
```

PowerShell:

```powershell
docker run --rm -v "${PWD}:/src" -w /src devkitpro/devkita64:latest bash -lc 'source /opt/devkitpro/switchvars.sh && make -f Makefile.switch-sdmc SWITCH_COOPNET=1 switch-nro -j1'
```

The NRO is written to `build/us_switch/sm64coopdx.nro`, and its CoopNet identity is written to `build-logs/switch-coopnet-identity.txt`.

## Create a release distribution

Run the release target after all NRO-affecting changes are final:

```sh
make -f Makefile.switch-sdmc SWITCH_COOPNET=1 switch-dist -j1
```

The target validates the NRO0 header, confirms that the identity manifest's size and SHA-256 match the exact NRO, verifies the embedded icon result, and creates:

```text
dist/sm64coopdx-switch-1.5.1-switch/
dist/sm64coopdx-switch-1.5.1-switch.zip
```

The ZIP contains the NRO, icon, language files, logs directory marker, installation guide, identity manifest, and SHA-256 checksums. It deliberately excludes `baserom.us.z64`, debug ELF/map files, caches, and build logs.

## CI

`.github/workflows/build-switch-game.yml` is the authoritative Switch CI workflow. Every push to `main` runs the asset-free Horizon portability gate, so Switch regressions cannot be hidden by path filtering on the main branch. Pull requests to `main` run the same portability gate when Switch-relevant build, source, tool or workflow files change.

The workflow has two gates:

1. **Horizon portability gates**, always run without a ROM. They build real libnx NRO probes for the SDL2/GLES2/curl/zlib stack and platform/native-input layer, generate the Switch source overlays, and validate Switch-specific source invariants.
2. **Full Horizon game NRO**, runs on `main` pushes or manual dispatches when repository secret `SM64_BASEROM_US_URL` is configured. The secret must be a private HTTPS URL for the owner's legally obtained US baserom. CI verifies its SHA-1, builds and validates the full NRO, removes the ROM, and retains only build metadata/hashes rather than the baserom-backed binaries.

The old standalone bootstrap, platform-probe and portlibs-probe workflows were consolidated into this workflow to avoid duplicate jobs and conflicting status names.

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

## Runtime notes

- Private-lobby passwords use the in-game Switch keypad instead of the unstable native keyboard path.
- The Switch client sends the genuine NRO fingerprint recorded in the identity manifest and uses the `sm64coop-android` public lobby namespace recommended for unofficial ports. Desktop builds keep their existing namespace.
- Persistent startup, CoopNet, ROM-asset, checkpoint, and exception diagnostics are written under `sdmc:/switch/sm64coopdx/logs/`.
- Applet mode has less usable memory; use title override/full application mode, especially with mods.
