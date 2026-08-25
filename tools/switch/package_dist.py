#!/usr/bin/env python3
"""Create a validated, ROM-free Nintendo Switch distribution."""

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


def manifest_value(text: str, label: str) -> str:
    prefix = f"{label}:"
    for line in text.splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    raise SystemExit(f"identity manifest is missing {label!r}")


def safe_output(root: Path, path: Path, suffix: str = "") -> Path:
    resolved = path.resolve()
    dist_root = (root / "dist").resolve()
    if resolved.parent != dist_root or not resolved.name.startswith("sm64coopdx-switch-"):
        raise SystemExit(f"refusing unsafe distribution output path: {resolved}")
    if suffix and resolved.suffix != suffix:
        raise SystemExit(f"distribution output must end in {suffix}: {resolved}")
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--nro", type=Path, required=True)
    parser.add_argument("--icon", type=Path, required=True)
    parser.add_argument("--lang-dir", type=Path, required=True)
    parser.add_argument("--identity", type=Path, required=True)
    parser.add_argument("--install-guide", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--output-zip", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    nro = (root / args.nro).resolve()
    icon = (root / args.icon).resolve()
    lang_dir = (root / args.lang_dir).resolve()
    identity = (root / args.identity).resolve()
    install_guide = (root / args.install_guide).resolve()
    output_dir = safe_output(root, root / args.output_dir)
    output_zip = safe_output(root, root / args.output_zip, ".zip")

    require_file(nro, "NRO")
    require_file(icon, "icon")
    require_file(identity, "identity manifest")
    require_file(install_guide, "installation guide")
    languages = sorted(lang_dir.glob("*.ini"))
    if not languages:
        raise SystemExit(f"no language files found in {lang_dir}")

    with nro.open("rb") as source:
        source.seek(16)
        if source.read(4) != b"NRO0":
            raise SystemExit(f"invalid NRO0 header: {nro}")

    nro_size = nro.stat().st_size
    nro_hash = sha256(nro)
    identity_text = identity.read_text(encoding="utf-8")
    if manifest_value(identity_text, "NRO size") != str(nro_size):
        raise SystemExit("identity manifest NRO size does not match the release NRO")
    if manifest_value(identity_text, "NRO SHA-256").lower() != nro_hash:
        raise SystemExit("identity manifest SHA-256 does not match the release NRO")
    if manifest_value(identity_text, "Embedded icon present").lower() != "yes":
        raise SystemExit("identity manifest does not verify an embedded icon")

    if output_dir.exists():
        shutil.rmtree(output_dir)
    if output_zip.exists():
        output_zip.unlink()

    app_dir = output_dir / "switch" / "sm64coopdx"
    logs_dir = app_dir / "logs"
    lang_output = app_dir / "lang"
    logs_dir.mkdir(parents=True)
    lang_output.mkdir(parents=True)

    shutil.copy2(nro, app_dir / "sm64coopdx.nro")
    shutil.copy2(icon, app_dir / "icon.jpg")
    for language in languages:
        shutil.copy2(language, lang_output / language.name)
    shutil.copy2(identity, output_dir / "switch-coopnet-identity.txt")
    shutil.copy2(install_guide, output_dir / "INSTALL.md")
    (logs_dir / "README.txt").write_text(
        "SM64CoopDX writes Switch runtime diagnostics in this directory.\n",
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

    print(f"Packaged NRO: {nro_size} bytes, SHA-256 {nro_hash}")
    print(f"Distribution folder: {output_dir}")
    print(f"Distribution ZIP: {output_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
