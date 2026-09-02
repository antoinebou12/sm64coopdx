#!/usr/bin/env python3
"""Stage a combined console distribution with Switch NRO and New 3DS binaries."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import zipfile
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--switch-dist", type=Path, required=True)
    parser.add_argument("--new3ds-dist", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    switch_dist = (root / args.switch_dist).resolve()
    new3ds_dist = (root / args.new3ds_dist).resolve()
    output_dir = (root / "dist" / f"sm64coopdx-console-{args.version}").resolve()
    output_zip = output_dir.with_suffix(".zip")

    switch_nro = switch_dist / "switch" / "sm64coopdx" / "sm64coopdx.nro"
    new3ds_3dsx = new3ds_dist / "3ds" / "sm64coopdx" / "sm64coopdx.3dsx"
    new3ds_cia = new3ds_dist / "3ds" / "sm64coopdx" / "sm64coopdx.cia"

    for path, label in (
        (switch_nro, "Switch NRO"),
        (new3ds_3dsx, "New 3DS 3DSX"),
        (new3ds_cia, "New 3DS CIA"),
    ):
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"missing or empty {label}: {path}")

    if output_dir.exists():
        shutil.rmtree(output_dir)
    if output_zip.exists():
        output_zip.unlink()

    switch_out = output_dir / "switch" / "sm64coopdx"
    new3ds_out = output_dir / "3ds" / "sm64coopdx"
    switch_out.mkdir(parents=True)
    new3ds_out.mkdir(parents=True)

    shutil.copy2(switch_nro, switch_out / "sm64coopdx.nro")
    shutil.copy2(new3ds_3dsx, new3ds_out / "sm64coopdx.3dsx")
    shutil.copy2(new3ds_cia, new3ds_out / "sm64coopdx.cia")

    if (switch_dist / "INSTALL.md").is_file():
        shutil.copy2(switch_dist / "INSTALL.md", output_dir / "INSTALL-switch.md")
    if (new3ds_dist / "INSTALL.md").is_file():
        shutil.copy2(new3ds_dist / "INSTALL.md", output_dir / "INSTALL-new3ds.md")

    packaged_files = sorted(path for path in output_dir.rglob("*") if path.is_file())
    checksums = "".join(
        f"{sha256(path)}  {path.relative_to(output_dir).as_posix()}\n"
        for path in packaged_files
    )
    (output_dir / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8")

    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in packaged_files:
            archive.write(path, path.relative_to(output_dir).as_posix())

    print(f"Combined console distribution: {output_dir}")
    print(f"Combined console ZIP: {output_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
