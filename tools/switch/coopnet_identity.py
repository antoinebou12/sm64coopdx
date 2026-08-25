#!/usr/bin/env python3
"""Generate and verify the Switch CoopNet executable identity manifest."""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
from pathlib import Path
from typing import NamedTuple

MURMUR_MULTIPLIER = 0xC6A4A7935BD1E995
MURMUR_SHIFT = 47
LIBSTDCXX_SEED = 0xC70F6907
MASK64 = (1 << 64) - 1
READ_CHUNK_SIZE = 64 * 1024


def _mix_block(hash_value: int, block: int) -> int:
    block = (block * MURMUR_MULTIPLIER) & MASK64
    block ^= block >> MURMUR_SHIFT
    block = (block * MURMUR_MULTIPLIER) & MASK64
    hash_value ^= block
    return (hash_value * MURMUR_MULTIPLIER) & MASK64


def murmur64a_file(path: Path) -> tuple[int, int]:
    """Match libstdc++ `_Hash_bytes` without loading the file into memory."""
    size = path.stat().st_size
    if size == 0:
        return 0, 0

    hash_value = (LIBSTDCXX_SEED ^ (size * MURMUR_MULTIPLIER)) & MASK64
    tail = bytearray()
    processed = 0
    with path.open("rb") as stream:
        while chunk := stream.read(READ_CHUNK_SIZE):
            processed += len(chunk)
            data = tail + chunk
            block_end = len(data) - (len(data) % 8)
            for offset in range(0, block_end, 8):
                block = int.from_bytes(data[offset : offset + 8], "little")
                hash_value = _mix_block(hash_value, block)
            tail = bytearray(data[block_end:])

    if processed != size:
        raise OSError(f"short read: expected {size} bytes, processed {processed}")
    if tail:
        hash_value ^= int.from_bytes(tail, "little")
        hash_value = (hash_value * MURMUR_MULTIPLIER) & MASK64

    hash_value ^= hash_value >> MURMUR_SHIFT
    hash_value = (hash_value * MURMUR_MULTIPLIER) & MASK64
    hash_value ^= hash_value >> MURMUR_SHIFT
    return hash_value & MASK64, processed


class NroAssetInfo(NamedTuple):
    nro_size: int
    aset_offset: int
    icon_offset: int
    icon_size: int
    icon_sha256: str


def inspect_nro(path: Path) -> NroAssetInfo:
    file_size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(0x20)
        if len(header) < 0x20 or header[0x10:0x14] != b"NRO0":
            raise ValueError("NRO0 header is missing or truncated")
        nro_size = struct.unpack_from("<I", header, 0x18)[0]
        if nro_size < 0x20 or nro_size + 56 > file_size:
            raise ValueError("declared NRO size does not leave room for ASET")

        stream.seek(nro_size)
        aset = stream.read(56)
        magic, version, icon_offset, icon_size, *_ = struct.unpack("<4sI6Q", aset)
        if magic != b"ASET" or version != 0:
            raise ValueError("valid ASET header is missing after NRO0 image")
        icon_start = nro_size + icon_offset
        icon_end = icon_start + icon_size
        if icon_size == 0 or icon_start < nro_size + 56 or icon_end > file_size:
            raise ValueError("ASET icon range is empty or outside the NRO")

        stream.seek(icon_start)
        if stream.read(2) != b"\xff\xd8":
            raise ValueError("embedded ASET icon is not a JPEG")
        stream.seek(icon_start)
        digest = hashlib.sha256()
        remaining = icon_size
        while remaining:
            chunk = stream.read(min(READ_CHUNK_SIZE, remaining))
            if not chunk:
                raise ValueError("embedded ASET icon is truncated")
            digest.update(chunk)
            remaining -= len(chunk)

    return NroAssetInfo(nro_size, nro_size, icon_offset, icon_size, digest.hexdigest())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(READ_CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def git_commit(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def write_manifest(
    nro: Path,
    output: Path,
    root: Path,
    coopnet_commit: str,
    version: str,
) -> int:
    fingerprint, processed = murmur64a_file(nro)
    if fingerprint == 0 or processed != nro.stat().st_size:
        raise ValueError("computed Switch CoopNet fingerprint is zero or incomplete")
    assets = inspect_nro(nro)
    sha256 = sha256_file(nro)
    commit = git_commit(root)
    size = nro.stat().st_size
    authorization_line = (
        f"UINT64_C({fingerprint}), // SM64CoopDX Switch {commit}"
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "\n".join(
            (
                "SM64CoopDX Switch CoopNet Identity",
                f"Build NRO path: {nro}",
                "Runtime NRO path: sdmc:/switch/sm64coopdx/sm64coopdx.nro",
                f"NRO size: {size}",
                f"NRO SHA-256: {sha256}",
                f"CoopNet fingerprint decimal: {fingerprint}",
                f"CoopNet fingerprint hexadecimal: 0x{fingerprint:016x}",
                f"Fingerprint bytes processed: {processed}",
                f"Game commit: {commit}",
                f"CoopNet pin: {coopnet_commit}",
                f"Game version: {version}",
                f"NRO0 declared size: {assets.nro_size}",
                f"ASET offset: {assets.aset_offset}",
                "Embedded icon present: yes",
                f"Embedded icon offset: {assets.icon_offset}",
                f"Embedded icon size: {assets.icon_size}",
                f"Embedded icon SHA-256: {assets.icon_sha256}",
                "",
                "Official CoopNet server allowlist line:",
                authorization_line,
                "",
            )
        ),
        encoding="utf-8",
    )
    return fingerprint


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nro", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--coopnet-commit", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    fingerprint = write_manifest(
        args.nro.resolve(),
        args.output.resolve(),
        args.root.resolve(),
        args.coopnet_commit,
        args.version,
    )
    print(f"Switch CoopNet fingerprint: {fingerprint} (0x{fingerprint:016x})")
    print(f"Identity manifest: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
