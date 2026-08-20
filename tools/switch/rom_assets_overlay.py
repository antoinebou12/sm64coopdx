#!/usr/bin/env python3
"""Generate a Switch-debug overlay for src/pc/rom_assets.c.

The normal desktop source stays untouched. The SD-card debug build uses this
copy to turn ROM I/O/segment failures into durable diagnostics instead of an
unchecked FILE*/read or an endless out-of-range asset loop.
"""

from __future__ import annotations

import sys
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_function(text: str, start_marker: str, end_marker: str, replacement: str, label: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError(f"{label}: start marker not found")
    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:start] + replacement.rstrip() + "\n\n" + text[end:]


def transform(text: str) -> str:
    text = replace_once(
        text,
        '#include "utils/misc.h"\n',
        '#include "utils/misc.h"\n#include <string.h>\n#include "pc/platform/switch/switch_rom_asset_trace.h"\n',
        "trace include",
    )

    text = replace_once(
        text,
        'static FILE* sRomFile = NULL;\nstatic struct RomAsset* sRomAssets = NULL;\n',
        'static FILE* sRomFile = NULL;\nstatic long sRomFileSize = -1;\nstatic struct RomAsset* sRomAssets = NULL;\n',
        "ROM file size state",
    )

    text = replace_once(
        text,
        '''static s16 READ16(struct RomAsset* asset) {\n    s64 index = (asset->segmentedAddress + asset->cursor);\n    if (index < 0 || index >= sCurrentSegmentSize) { return 0; }\n    u8* ptr = &sCurrentSegmentMemory[index];\n    s16 value = BSWAP16(*((s16*)ptr));\n    asset->cursor += sizeof(s16);\n    return value;\n}\n''',
        '''static s16 READ16(struct RomAsset* asset) {\n    s64 index = (asset->segmentedAddress + asset->cursor);\n    if (index < 0 || (u64)index + sizeof(s16) > sCurrentSegmentSize) {\n        switch_rom_asset_trace_printf(\n            "phase=READ16 bounds failure\\nphysical=0x%08x size=0x%08x\\nsegmented=0x%08x asset_size=0x%08x cursor=0x%08x decoded_size=0x%08x",\n            asset->physicalAddress, asset->physicalSize, asset->segmentedAddress,\n            asset->segmentedSize, asset->cursor, sCurrentSegmentSize);\n        asset->cursor = asset->segmentedSize;\n        return 0;\n    }\n    s16 raw = 0;\n    memcpy(&raw, &sCurrentSegmentMemory[index], sizeof(raw));\n    s16 value = BSWAP16(raw);\n    asset->cursor += sizeof(s16);\n    return value;\n}\n''',
        "READ16 hardening",
    )

    text = replace_once(
        text,
        '''static s8 READ8(struct RomAsset* asset) {\n    s64 index = (asset->segmentedAddress + asset->cursor);\n    if (index < 0 || index >= sCurrentSegmentSize) { return 0; }\n    u8* ptr = &sCurrentSegmentMemory[index];\n    s8 value = *ptr;\n    asset->cursor += sizeof(s8);\n    return value;\n}\n''',
        '''static s8 READ8(struct RomAsset* asset) {\n    s64 index = (asset->segmentedAddress + asset->cursor);\n    if (index < 0 || (u64)index + sizeof(s8) > sCurrentSegmentSize) {\n        switch_rom_asset_trace_printf(\n            "phase=READ8 bounds failure\\nphysical=0x%08x size=0x%08x\\nsegmented=0x%08x asset_size=0x%08x cursor=0x%08x decoded_size=0x%08x",\n            asset->physicalAddress, asset->physicalSize, asset->segmentedAddress,\n            asset->segmentedSize, asset->cursor, sCurrentSegmentSize);\n        asset->cursor = asset->segmentedSize;\n        return 0;\n    }\n    s8 value = (s8)sCurrentSegmentMemory[index];\n    asset->cursor += sizeof(s8);\n    return value;\n}\n''',
        "READ8 hardening",
    )

    segment_impl = r'''static bool rom_asset_load_segment(u32 physicalAddress, u32 physicalSize) {
    if (physicalAddress == sCurrentPhysicalAddress &&
        physicalSize == sCurrentPhysicalSize &&
        sCurrentSegmentMemory != NULL) {
        return true;
    }

    switch_rom_asset_trace_printf(
        "phase=segment read begin\nphysical=0x%08x\nphysical_size=0x%08x\nrom_size=%ld",
        physicalAddress, physicalSize, sRomFileSize);

    if (sRomFile == NULL || physicalSize < sizeof(u32) || sRomFileSize <= 0 ||
        (u64)physicalAddress + (u64)physicalSize > (u64)sRomFileSize) {
        switch_rom_asset_trace_printf(
            "phase=segment rejected\nphysical=0x%08x\nphysical_size=0x%08x\nrom_size=%ld",
            physicalAddress, physicalSize, sRomFileSize);
        return false;
    }

    if (sCurrentSegmentMemory != NULL) {
        free(sCurrentSegmentMemory);
        sCurrentSegmentMemory = NULL;
    }
    sCurrentSegmentSize = 0;
    sCurrentPhysicalAddress = 0;
    sCurrentPhysicalSize = 0;

    sCurrentSegmentMemory = malloc(physicalSize);
    if (sCurrentSegmentMemory == NULL) {
        switch_rom_asset_trace_printf(
            "phase=segment malloc failed\nphysical=0x%08x\nphysical_size=0x%08x",
            physicalAddress, physicalSize);
        LOG_ERROR("Could not allocate segment memory!");
        return false;
    }

    if (fseek(sRomFile, (long)physicalAddress, SEEK_SET) != 0) {
        switch_rom_asset_trace_printf(
            "phase=segment seek failed\nphysical=0x%08x\nphysical_size=0x%08x",
            physicalAddress, physicalSize);
        free(sCurrentSegmentMemory);
        sCurrentSegmentMemory = NULL;
        return false;
    }

    const size_t bytesRead = fread(sCurrentSegmentMemory, sizeof(u8), physicalSize, sRomFile);
    if (bytesRead != physicalSize) {
        switch_rom_asset_trace_printf(
            "phase=segment short read\nphysical=0x%08x\nphysical_size=0x%08x\nbytes_read=%zu\nferror=%d",
            physicalAddress, physicalSize, bytesRead, ferror(sRomFile));
        free(sCurrentSegmentMemory);
        sCurrentSegmentMemory = NULL;
        return false;
    }

    sCurrentPhysicalAddress = physicalAddress;
    sCurrentPhysicalSize = physicalSize;
    sCurrentSegmentSize = physicalSize;

    u8* decompressed = rom_assets_decompress((u32*)sCurrentSegmentMemory, &sCurrentSegmentSize);
    if (decompressed != NULL) {
        free(sCurrentSegmentMemory);
        sCurrentSegmentMemory = decompressed;
    }

    if (sCurrentSegmentMemory == NULL || sCurrentSegmentSize == 0) {
        switch_rom_asset_trace_printf(
            "phase=segment decode failed\nphysical=0x%08x\nphysical_size=0x%08x",
            physicalAddress, physicalSize);
        sCurrentPhysicalAddress = 0;
        sCurrentPhysicalSize = 0;
        return false;
    }

    switch_rom_asset_trace_printf(
        "phase=segment ready\nphysical=0x%08x\nphysical_size=0x%08x\ndecoded_size=0x%08x",
        physicalAddress, physicalSize, sCurrentSegmentSize);
    return true;
}'''

    text = replace_function(
        text,
        "static bool rom_asset_load_segment(u32 physicalAddress, u32 physicalSize) {",
        "// Some Vtx arrays",
        segment_impl,
        "segment loader",
    )

    text = replace_once(
        text,
        '''static void rom_asset_load(struct RomAsset* asset) {\n    if (!rom_asset_load_segment(asset->physicalAddress, asset->physicalSize)) {\n        return;\n    }\n''',
        '''static void rom_asset_load(struct RomAsset* asset) {\n    if (!rom_asset_load_segment(asset->physicalAddress, asset->physicalSize)) {\n        return;\n    }\n\n    if ((u64)asset->segmentedAddress + (u64)asset->segmentedSize > (u64)sCurrentSegmentSize) {\n        switch_rom_asset_trace_printf(\n            "phase=asset range rejected\\ntype=%u\\nphysical=0x%08x\\nphysical_size=0x%08x\\nsegmented=0x%08x\\nasset_size=0x%08x\\ndecoded_size=0x%08x",\n            asset->assetType, asset->physicalAddress, asset->physicalSize,\n            asset->segmentedAddress, asset->segmentedSize, sCurrentSegmentSize);\n        return;\n    }\n''',
        "asset range validation",
    )

    load_impl = r'''void rom_assets_load(void) {
    LOG_INFO("loading asset");

    switch_rom_asset_trace_printf("phase=rom open begin\npath=%s", gRomFilename);

    if (gRomFilename[0] == '\0' || !fs_sys_file_exists(gRomFilename)) {
        switch_rom_asset_trace_printf("phase=rom open rejected\nreason=path missing\npath=%s", gRomFilename);
        LOG_ERROR("ROM asset path does not exist: %s", gRomFilename);
        return;
    }

    sRomFile = fopen(gRomFilename, "rb");
    if (sRomFile == NULL) {
        switch_rom_asset_trace_printf("phase=rom fopen failed\npath=%s", gRomFilename);
        LOG_ERROR("Could not open ROM asset file: %s", gRomFilename);
        return;
    }

    if (fseek(sRomFile, 0, SEEK_END) != 0) {
        switch_rom_asset_trace_printf("phase=rom size seek failed\npath=%s", gRomFilename);
        fclose(sRomFile);
        sRomFile = NULL;
        return;
    }

    sRomFileSize = ftell(sRomFile);
    if (sRomFileSize != (8L * 1024L * 1024L)) {
        switch_rom_asset_trace_printf(
            "phase=rom size rejected\npath=%s\nsize=%ld\nexpected=%ld",
            gRomFilename, sRomFileSize, 8L * 1024L * 1024L);
        fclose(sRomFile);
        sRomFile = NULL;
        sRomFileSize = -1;
        return;
    }

    if (fseek(sRomFile, 0, SEEK_SET) != 0) {
        switch_rom_asset_trace_printf("phase=rom rewind failed\npath=%s", gRomFilename);
        fclose(sRomFile);
        sRomFile = NULL;
        sRomFileSize = -1;
        return;
    }

    switch_rom_asset_trace_printf("phase=rom open complete\npath=%s\nsize=%ld", gRomFilename, sRomFileSize);

    u32 assetIndex = 0;
    while (sRomAssets != NULL) {
        struct RomAsset* asset = sRomAssets;
        switch_rom_asset_trace_printf(
            "phase=asset begin\nindex=%u\ntype=%u\nphysical=0x%08x\nphysical_size=0x%08x\nsegmented=0x%08x\nasset_size=0x%08x\ncursor=0x%08x",
            assetIndex, asset->assetType, asset->physicalAddress, asset->physicalSize,
            asset->segmentedAddress, asset->segmentedSize, asset->cursor);

        rom_asset_load(asset);

        struct RomAsset* next = asset->next;
        free(asset);
        sRomAssets = next;
        assetIndex++;
    }

    if (sCurrentSegmentMemory != NULL) {
        free(sCurrentSegmentMemory);
        sCurrentSegmentMemory = NULL;
    }
    sCurrentSegmentSize = 0;
    sCurrentPhysicalAddress = 0;
    sCurrentPhysicalSize = 0;

    fclose(sRomFile);
    sRomFile = NULL;
    sRomFileSize = -1;

    switch_rom_asset_trace_printf("phase=rom assets complete\nassets=%u", assetIndex);
}'''

    text = replace_function(
        text,
        "void rom_assets_load(void) {",
        "void rom_assets_queue(void* ptr",
        load_impl,
        "rom_assets_load",
    )

    return text


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")

    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    transformed = transform(source.read_text(encoding="utf-8"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(transformed, encoding="utf-8")


if __name__ == "__main__":
    main()
