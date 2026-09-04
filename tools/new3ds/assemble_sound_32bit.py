#!/usr/bin/env python3
"""Assemble 32-bit little-endian CTL/TBL/sequences for the New 3DS port."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def run(cmd, cwd=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=cwd)


def has_vanilla_samples(root: Path) -> bool:
    sfx1 = root / "sound" / "samples" / "sfx_1"
    return sfx1.is_dir() and any(sfx1.glob("*.aiff"))


def encode_aiffs(root: Path, build_dir: Path, tools_dir: Path) -> Path:
    src_root = root / "sound" / "samples"
    dest_root = build_dir / "sound" / "samples"
    codebook = tools_dir / "aiff_extract_codebook"
    vadpcm = tools_dir / "vadpcm_enc"
    if os.name == "nt":
        if (tools_dir / "aiff_extract_codebook.exe").exists():
            codebook = tools_dir / "aiff_extract_codebook.exe"
        if (tools_dir / "vadpcm_enc.exe").exists():
            vadpcm = tools_dir / "vadpcm_enc.exe"

    count = 0
    for aiff in sorted(src_root.rglob("*.aiff")):
        rel = aiff.relative_to(src_root)
        dest_aifc = dest_root / rel.with_suffix(".aifc")
        dest_table = dest_root / rel.with_suffix(".table")
        dest_aifc.parent.mkdir(parents=True, exist_ok=True)
        if dest_aifc.exists() and dest_aifc.stat().st_mtime >= aiff.stat().st_mtime:
            count += 1
            continue
        with open(dest_table, "w", encoding="utf-8") as table:
            subprocess.run([str(codebook), str(aiff)], check=True, stdout=table)
        run([str(vadpcm), "-c", str(dest_table), str(aiff), str(dest_aifc)])
        count += 1
    if count == 0:
        sys.exit("No AIFF samples found under sound/samples after extract_assets.py")
    print(f"Encoded {count} AIFC sample files into {dest_root}", flush=True)
    return dest_root


def collect_sequences(root: Path, build_dir: Path, version: str) -> list[str]:
    files = []
    seen_names = set()
    search_dirs = [
        build_dir / "sound" / "sequences",
        root / "sound" / "sequences" / version,
        root / "sound" / "sequences",
    ]
    for directory in search_dirs:
        if not directory.is_dir():
            continue
        for m64 in sorted(directory.rglob("*.m64")):
            name = m64.stem
            if name in seen_names:
                continue
            seen_names.add(name)
            files.append(str(m64))
    if "00_sound_player" not in seen_names:
        sys.exit("Missing 00_sound_player.m64 (assemble sound/sequences/00_sound_player.s first)")
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--sound-bin-dir", required=True)
    parser.add_argument("--version", default="us")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    build_dir = Path(args.build_dir).resolve()
    sound_bin = Path(args.sound_bin_dir).resolve()
    tools_dir = root / "tools"
    version = args.version
    upper = version.upper()

    os.chdir(root)
    sound_bin.mkdir(parents=True, exist_ok=True)

    if not has_vanilla_samples(root):
        if not (root / f"baserom.{version}.z64").exists():
            sys.exit(f"baserom.{version}.z64 is required to extract sound samples")
        run([sys.executable, str(root / "extract_assets.py"), version])

    run([sys.executable, str(tools_dir / "copy_extended_sounds.py")])
    run(["make", "-s", "-C", str(tools_dir), "CC=gcc", "CXX=g++", "aiff_extract_codebook", "vadpcm_enc"])

    samples_dir = encode_aiffs(root, build_dir, tools_dir)

    assemble = [
        sys.executable,
        str(tools_dir / "assemble_sound.py"),
        "--endian",
        "little",
        "--bitwidth",
        "32",
        "--raw-output",
        "--offsets-dir",
        str(build_dir),
        f"-DVERSION_{upper}",
    ]

    run(
        assemble
        + [
            str(samples_dir),
            str(root / "sound" / "sound_banks"),
            str(sound_bin / "sound_data.ctl"),
            str(sound_bin / "ctl_header"),
            str(sound_bin / "sound_data.tbl"),
            str(sound_bin / "tbl_header"),
        ]
    )

    sequences = collect_sequences(root, build_dir, version)
    run(
        assemble
        + [
            "--sequences",
            str(sound_bin / "sequences.bin"),
            str(sound_bin / "sequences_header"),
            str(sound_bin / "bank_sets"),
            str(root / "sound" / "sound_banks"),
            str(root / "sound" / "sequences.json"),
        ]
        + sequences
    )

    for required in ("sound_data.ctl", "sound_data.tbl", "sequences.bin", "bank_sets"):
        path = sound_bin / required
        if not path.is_file() or path.stat().st_size == 0:
            sys.exit(f"32-bit sound assemble failed to produce {path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
