# Switch Mod Compatibility Changes

## Summary
Implemented generic mod compatibility improvements for Switch build to support large mod packs like mods.rar (62 mods, 5,656 files).

## Changes Made

### src/pc/network/packets/packet_mod_list.c
- Increased caps:
  - MAX_REMOTE_MOD_FILES from 20000 to 65535
  - MAX_REMOTE_TOTAL_FILES from 30000 to 100000
- Added targeted manifest diagnostics:
  - MOD_MANIFEST_BEGIN version mod_count
  - MOD_MANIFEST_ENTRY mod_index name
  - MOD_MANIFEST_ENTRY_FILES mod_index file_count
  - MOD_MANIFEST_FILES mod_index files_received/files_count every 256 files
  - MOD_MANIFEST_DONE total_mods total_files total_bytes
- Path safety already enforced via network_remote_mod_path_is_safe

### Logging
- Switch CoopNet logging already samples tx/rx every SWITCH_COOPNET_COMMIT_INTERVAL (128)
- Per-packet logging reduced to first 5 + every 128 packets

## Next Steps
- Rebuild Switch NRO: build/us_switch/sm64coopdx.nro
- Test with mods.rar lobby
- Monitor coopnet.log for MOD_MANIFEST_* checkpoints

## Acceptance Criteria
- Lobby join → ICE/TURN → mod-list request → manifest complete for 62 mods / 5,656 files
- No manifest rejection due to file count caps
- Semantic logging instead of per-packet spam
