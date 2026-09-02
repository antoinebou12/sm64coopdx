#!/usr/bin/env python3
"""Create a ROM-free New Nintendo 3DS distribution folder and ZIP."""

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


def require_file(path: Path, label: str) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f"missing or empty {label}: {path}")


def safe_output(root: Path, path: Path, suffix: str = "") -> Path:
    resolved = path.resolve()
    dist_root = (root / "dist").resolve()
    if resolved.parent != dist_root or not resolved.name.startswith("sm64coopdx-new3ds-"):
        raise SystemExit(f"refusing unsafe distribution output path: {resolved}")
    if suffix and resolved.suffix != suffix:
        raise SystemExit(f"distribution output must end in {suffix}: {resolved}")
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--3dsx", dest="tdsx", type=Path, required=True)
    parser.add_argument("--cia", type=Path, default=None)
    parser.add_argument("--smdh", type=Path, required=True)
    parser.add_argument("--install-guide", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--output-zip", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    tdsx = (root / args.tdsx).resolve()
    cia = (root / args.cia).resolve() if args.cia else None
    smdh = (root / args.smdh).resolve()
    install_guide = (root / args.install_guide).resolve()
    output_dir = safe_output(root, root / args.output_dir)
    output_zip = safe_output(root, root / args.output_zip, ".zip")

    require_file(tdsx, "3DSX")
    require_file(smdh, "SMDH")
    require_file(install_guide, "installation guide")
    if cia is not None:
        require_file(cia, "CIA")

    if output_dir.exists():
        shutil.rmtree(output_dir)
    if output_zip.exists():
        output_zip.unlink()

    app_dir = output_dir / "3ds" / "sm64coopdx"
    logs_dir = app_dir / "logs"
    logs_dir.mkdir(parents=True)

    shutil.copy2(tdsx, app_dir / "sm64coopdx.3dsx")
    shutil.copy2(smdh, app_dir / "sm64coopdx.smdh")
    if cia is not None:
        shutil.copy2(cia, app_dir / "sm64coopdx.cia")
    shutil.copy2(install_guide, output_dir / "INSTALL.md")
    (logs_dir / "README.txt").write_text(
        "SM64CoopDX writes New 3DS runtime diagnostics under sdmc:/3ds/sm64coopdx/logs/.\n",
        encoding="utf-8",
    )

    packaged_files = sorted(path for path in output_dir.rglob("*") if path.is_file())
    checksums = "".join(
        f"{sha256(path)}  {path.relative_to(output_dir).as_posix()}\n"
        for path in packaged_files
    )
    (output_dir / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8")

    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(item for item in output_dir.rglob("*") if item.is_file()):
            archive.write(path, path.relative_to(output_dir).as_posix())

    if any("baserom" in path.name.lower() for path in output_dir.rglob("*")):
        raise SystemExit("ROM data unexpectedly entered the distribution")

    print(f"Packaged 3DSX: {tdsx.stat().st_size} bytes, SHA-256 {sha256(tdsx)}")
    if cia is not None:
        print(f"Packaged CIA: {cia.stat().st_size} bytes, SHA-256 {sha256(cia)}")
    print(f"Distribution folder: {output_dir}")
    print(f"Distribution ZIP: {output_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
