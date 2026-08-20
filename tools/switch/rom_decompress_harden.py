#!/usr/bin/env python3
"""Harden the Switch-debug MIO0 ROM decompressor.

The normal ROM is still MD5-validated. This extra layer prevents a malformed
segment header, an allocation failure, or a bad back-reference from becoming an
ARM64 memory fault while the hardware-debug build is loading ROM assets.
"""

from __future__ import annotations

import sys
from pathlib import Path

MARKER = "u8* rom_assets_decompress(u32* data, u32* decompressedSize) {"

SAFE_DECOMPRESSOR = r'''u8* rom_assets_decompress(u32* data, u32* decompressedSize) {
    if (data == NULL || decompressedSize == NULL) {
        return NULL;
    }

    const u32 sourceSize = *decompressedSize;
    if (sourceSize < 4) {
        return NULL;
    }

    u32 magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (BSWAP32(magic) != 0x4d494f30) {
        return NULL;
    }

    // A MIO0 header is four big-endian u32 values.
    if (sourceSize < 16) {
        switch_rom_asset_trace_printf(
            "phase=mio0 rejected\nreason=short header\nsource_size=0x%08x", sourceSize);
        *decompressedSize = 0;
        return NULL;
    }

    const u8* srcBytes = (const u8*)data;
    u32 rawOutputSize = 0;
    u32 rawCompOffset = 0;
    u32 rawRawOffset = 0;
    memcpy(&rawOutputSize, srcBytes + 4, sizeof(rawOutputSize));
    memcpy(&rawCompOffset, srcBytes + 8, sizeof(rawCompOffset));
    memcpy(&rawRawOffset, srcBytes + 12, sizeof(rawRawOffset));

    const u32 outputSize = BSWAP32(rawOutputSize);
    const u32 compOffset = BSWAP32(rawCompOffset);
    const u32 rawOffset = BSWAP32(rawRawOffset);

    // SM64's decoded segments are far smaller than this. The cap also keeps a
    // corrupt header from turning into an enormous Horizon allocation request.
    if (outputSize == 0 || outputSize > (64u * 1024u * 1024u) ||
        compOffset < 16 || compOffset > rawOffset || rawOffset > sourceSize) {
        switch_rom_asset_trace_printf(
            "phase=mio0 rejected\nreason=invalid header\nsource_size=0x%08x\noutput_size=0x%08x\ncomp_offset=0x%08x\nraw_offset=0x%08x",
            sourceSize, outputSize, compOffset, rawOffset);
        *decompressedSize = 0;
        return NULL;
    }

    u8* output = (u8*)malloc(outputSize);
    if (output == NULL) {
        switch_rom_asset_trace_printf(
            "phase=mio0 allocation failed\nsource_size=0x%08x\noutput_size=0x%08x",
            sourceSize, outputSize);
        *decompressedSize = 0;
        return NULL;
    }

    u32 controlPos = 16;
    u32 compPos = compOffset;
    u32 rawPos = rawOffset;
    u32 destPos = 0;
    u32 controlBits = 0;
    int bitsRemaining = 0;

    while (destPos < outputSize) {
        if (bitsRemaining == 0) {
            // Layout is [header][control words][backrefs][raw bytes].
            if ((u64)controlPos + sizeof(u32) > compOffset) {
                switch_rom_asset_trace_printf(
                    "phase=mio0 decode failed\nreason=control overrun\ndest=0x%08x\ncontrol=0x%08x\ncomp=0x%08x",
                    destPos, controlPos, compOffset);
                free(output);
                *decompressedSize = 0;
                return NULL;
            }
            memcpy(&controlBits, srcBytes + controlPos, sizeof(controlBits));
            controlBits = BSWAP32(controlBits);
            controlPos += sizeof(u32);
            bitsRemaining = 32;
        }

        if (controlBits & 0x80000000u) {
            if (rawPos >= sourceSize) {
                switch_rom_asset_trace_printf(
                    "phase=mio0 decode failed\nreason=raw overrun\ndest=0x%08x\nraw=0x%08x\nsource_size=0x%08x",
                    destPos, rawPos, sourceSize);
                free(output);
                *decompressedSize = 0;
                return NULL;
            }
            output[destPos++] = srcBytes[rawPos++];
        } else {
            if ((u64)compPos + sizeof(u16) > rawOffset) {
                switch_rom_asset_trace_printf(
                    "phase=mio0 decode failed\nreason=backref table overrun\ndest=0x%08x\ncomp=0x%08x\nraw=0x%08x",
                    destPos, compPos, rawOffset);
                free(output);
                *decompressedSize = 0;
                return NULL;
            }

            u16 rawParam = 0;
            memcpy(&rawParam, srcBytes + compPos, sizeof(rawParam));
            compPos += sizeof(u16);
            const u16 param = BSWAP16(rawParam);
            const u32 count = (param >> 12) + 3u;
            const u32 distance = (param & 0x0fffu) + 1u;

            if (distance > destPos || count > outputSize - destPos) {
                switch_rom_asset_trace_printf(
                    "phase=mio0 decode failed\nreason=invalid backref\ndest=0x%08x\ncount=0x%08x\ndistance=0x%08x\noutput_size=0x%08x",
                    destPos, count, distance, outputSize);
                free(output);
                *decompressedSize = 0;
                return NULL;
            }

            for (u32 i = 0; i < count; ++i) {
                output[destPos] = output[destPos - distance];
                destPos++;
            }
        }

        controlBits <<= 1;
        bitsRemaining--;
    }

    *decompressedSize = outputSize;
    return output;
}'''


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")

    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    text = source.read_text(encoding="utf-8")
    start = text.find(MARKER)
    if start < 0:
        raise RuntimeError("rom_assets_decompress marker not found")

    # rom_assets_decompress is the final function in src/pc/rom_assets.c.
    text = text[:start] + SAFE_DECOMPRESSOR + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
