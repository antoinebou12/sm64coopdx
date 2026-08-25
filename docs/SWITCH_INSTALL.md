# Installing SM64CoopDX on Nintendo Switch

## Requirements

- A homebrew-capable Nintendo Switch with a current Homebrew Menu setup.
- The SM64CoopDX Switch distribution ZIP.
- Your own legally obtained North American Super Mario 64 ROM matching the SHA-1 recorded in `sm64.us.sha1` in the source repository.

The ROM is copyrighted data and is not included in the distribution. Do not download, upload, or redistribute it.

## Install

1. Shut down the Switch before removing its SD card, or use a safe USB/FTP transfer method.
2. Extract the distribution ZIP directly to the root of the SD card. It creates `switch/sm64coopdx/`.
3. Copy your ROM to `switch/sm64coopdx/baserom.us.z64`.
4. Confirm this layout:

   ```text
   sd:/switch/sm64coopdx/
   ├── sm64coopdx.nro
   ├── baserom.us.z64       (supplied by you)
   ├── icon.jpg
   ├── lang/
   └── logs/
   ```

5. Put the SD card back in the Switch and start the Homebrew Menu in full application mode. With common Atmosphere setups, hold **R** while launching an installed game until Homebrew Menu opens. Album/applet mode has less memory and is not recommended.
6. Launch **SM64CoopDX**.

## CoopNet identity

The distribution includes `switch-coopnet-identity.txt` and `SHA256SUMS.txt`. Restricted public CoopNet servers must authorize the exact fingerprint in that identity file. Local/private servers and servers without an executable allowlist do not require that operator entry.

Do not modify the NRO after its fingerprint is authorized. Any byte change creates a different identity. Install the exact NRO supplied with the matching manifest.

## Updating

Replace `sm64coopdx.nro`, `icon.jpg`, and `lang/` with the files from the new distribution. Keep your private `baserom.us.z64`, mods, saves, configuration, and logs. A new NRO may require a new CoopNet allowlist fingerprint.

## Troubleshooting and logs

- If startup reports a missing or invalid ROM, verify its filename, region, and SHA-1.
- If the game freezes or runs out of memory, confirm it was launched in full application mode rather than Album/applet mode.
- Private-lobby passwords use the in-game Switch keypad; that flow should not open the native software keyboard.
- If public discovery works but admission is rejected, give the server operator the allowlist line from `switch-coopnet-identity.txt`.
- Runtime diagnostics are written under `switch/sm64coopdx/logs/`, including `startup.log`, `coopnet.log`, checkpoints, and `exception.log`.

When sharing diagnostics, remove lobby passwords, TURN credentials, and other private information first.
