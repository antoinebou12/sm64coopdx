#!/usr/bin/env python3
"""Generate and verify the final Switch CoopNet executable identity manifest."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

FNV1A64_OFFSET = 14695981039346656037
FNV1A64_PRIME = 1099511628211
CHUNK = 16 * 1024
MASK64 = (1 << 64) - 1


def fnv1a64(path: Path) -> tuple[int, int]:
    h = FNV1A64_OFFSET
    total = 0
    with path.open("rb") as f:
        while True:
            data = f.read(CHUNK)
            if not data:
                break
            total += len(data)
            for byte in data:
                h ^= byte
                h = (h * FNV1A64_PRIME) & MASK64
    if total == 0 or h == 0:
        raise ValueError("NRO is empty or produced a zero fingerprint")
    return h, total


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_nro(path: Path) -> dict[str, int | bool]:
    data = path.read_bytes()
    if len(data) < 0x70 or data[0x10:0x14] != b"NRO0":
        raise ValueError("missing NRO0 header at offset 0x10")
    image_size = struct.unpack_from("<I", data, 0x18)[0]
    if image_size <= 0 or image_size + 0x38 > len(data):
        raise ValueError(f"invalid NRO image size: {image_size}")
    if data[image_size:image_size + 4] != b"ASET":
        raise ValueError("missing ASET header at NRO image boundary")
    aset_version = struct.unpack_from("<I", data, image_size + 4)[0]
    if aset_version != 0:
        raise ValueError(f"unsupported ASET version: {aset_version}")
    icon_offset, icon_size = struct.unpack_from("<QQ", data, image_size + 8)
    nacp_offset, nacp_size = struct.unpack_from("<QQ", data, image_size + 24)
    if icon_size <= 0:
        raise ValueError("ASET icon section is empty")
    icon_start = image_size + icon_offset
    icon_end = icon_start + icon_size
    if icon_start < image_size + 0x38 or icon_end > len(data):
        raise ValueError("ASET icon section is out of bounds")
    icon = data[icon_start:icon_end]
    if not (icon.startswith(b"\xff\xd8") and icon.endswith(b"\xff\xd9")):
        raise ValueError("embedded icon is not a complete JPEG")
    if nacp_size <= 0 or image_size + nacp_offset + nacp_size > len(data):
        raise ValueError("ASET NACP section is missing or out of bounds")
    return {
        "nro0": True,
        "aset": True,
        "image_size": image_size,
        "icon_verified": True,
        "icon_size": icon_size,
        "nacp_size": nacp_size,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nro", required=True, type=Path)
    parser.add_argument("--output", default=Path("build-logs/switch-coopnet-identity.txt"), type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--coopnet-revision", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--expect-runtime-fingerprint")
    args = parser.parse_args()

    fingerprint, size = fnv1a64(args.nro)
    if args.expect_runtime_fingerprint:
        expected = int(args.expect_runtime_fingerprint, 16)
        if expected != fingerprint:
            raise SystemExit(
                f"runtime fingerprint mismatch: runtime={expected:016x} manifest={fingerprint:016x}"
            )
    structure = verify_nro(args.nro)
    sha256 = sha256_file(args.nro)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "Switch CoopNet executable identity",
        f"nro={args.nro}",
        f"fingerprint_hex={fingerprint:016x}",
        f"fingerprint_decimal={fingerprint}",
        f"sha256={sha256}",
        f"size={size}",
        f"commit={args.commit}",
        f"coopnet_revision={args.coopnet_revision}",
        f"version={args.version}",
        f"nro0_verified={'yes' if structure['nro0'] else 'no'}",
        f"aset_verified={'yes' if structure['aset'] else 'no'}",
        f"icon_verified={'yes' if structure['icon_verified'] else 'no'}",
        f"icon_size={structure['icon_size']}",
        f"nacp_size={structure['nacp_size']}",
        f"allowlist_entry=MPACKET_INFO.hash={fingerprint}",
    ]
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
