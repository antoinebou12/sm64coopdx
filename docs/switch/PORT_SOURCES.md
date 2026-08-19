# Switch Port Source Map

This branch is rebuilt from current `coop-deluxe/sm64coopdx:dev`. Older Nintendo Switch and split-screen branches are references, not merge bases.

## Source of truth

- Upstream: https://github.com/coop-deluxe/sm64coopdx
- Integration branch: `switch-next`
- Rule: upstream `dev` stays authoritative for normal game, networking and mod behavior.

## Reference implementations

### Original Nintendo Switch PR

- PR: https://github.com/coop-deluxe/sm64coopdx/pull/687
- Branch name at the time: `nx-support`
- What to mine:
  - Switch entry point and platform glue
  - controller integration
  - Switch socket backend
  - filesystem/path handling
  - software keyboard
  - NRO/NACP metadata
  - Switch-specific audio work
- Strategy: reimplement or transplant small isolated changes onto current upstream. Do not merge the old branch wholesale.

### sm64coopNX

- Repository: https://github.com/KakarottoCake/sm64coopnx
- What to mine:
  - current-ish devkitA64/libnx build knowledge
  - LDN local-wireless transport
  - LDN browser/host UI
  - Switch user/profile handling
  - recent Switch-specific fixes
- Strategy: port LDN behind a transport interface after ordinary BSD networking works on Switch.

### Split-screen Switch branch

- Repository: https://github.com/Isaac0-dev/sm64coopdx
- Branch: `splitscreen-switch`
- Reference commit inspected when this integration began: `3a326010871bc6e3fee1df36735ceb17523dfe9d`
- What to mine:
  - multiple local controllers
  - local-player state
  - multiple cameras/viewports
  - split HUD behavior
- Strategy: extract the concepts into platform-independent local-player code. Do not preserve hacks that force a fixed joystick count or tie local players directly to network connections.

## Integration rules

1. Keep Switch-only code concentrated under Switch-specific files/directories.
2. Avoid broad `#ifdef __SWITCH__` changes in game logic when an abstraction can isolate them.
3. Restore single-player Switch correctness before networking.
4. Restore ordinary Internet networking before LDN.
5. Introduce local-player state before split-screen rendering.
6. Add hybrid split-screen + online only after both networking and local multiplayer are independently stable.
7. Preserve attribution when code is adapted from an older branch.
8. Do not import old prebuilt `.a` libraries when the dependency can be rebuilt from source.
9. Keep proprietary ROM-extracted content out of Git and CI artifacts.

## Port history policy

When a reference implementation contains a clean, isolated commit that applies to current upstream, prefer a cherry-pick with attribution. When the surrounding architecture has changed materially, reimplement the behavior and record the original source in the commit message and this document.
