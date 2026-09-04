#!/usr/bin/env python3
"""Create a New Nintendo 3DS distribution folder and ZIP.

By default the packaged ZIP is ROM-free for CI and public release. Pass
--baserom for local installs that should ship baserom.us.z64 next to the app.
"""

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


def clear_output_tree(path: Path) -> None:
    """Remove a prior dist tree. Locked files (e.g. baserom open in an emulator) are skipped."""
    if not path.exists():
        return
    try:
        shutil.rmtree(path)
        return
    except OSError:
        pass
    for child in sorted(path.rglob("*"), reverse=True):
        try:
            if child.is_file() or child.is_symlink():
                child.unlink()
            elif child.is_dir():
                child.rmdir()
        except OSError:
            continue


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--3dsx", dest="tdsx", type=Path, required=True)
    parser.add_argument("--cia", type=Path, default=None)
    parser.add_argument("--smdh", type=Path, required=True)
    parser.add_argument("--lang-dir", type=Path, required=True)
    parser.add_argument("--install-guide", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--output-zip", type=Path, required=True)
    parser.add_argument(
        "--baserom",
        type=Path,
        default=None,
        help="optional local baserom.us.z64 to copy into the app folder (never commit ROMs)",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    tdsx = (root / args.tdsx).resolve()
    cia = (root / args.cia).resolve() if args.cia else None
    smdh = (root / args.smdh).resolve()
    lang_dir = (root / args.lang_dir).resolve()
    install_guide = (root / args.install_guide).resolve()
    baserom = (root / args.baserom).resolve() if args.baserom else None
    output_dir = safe_output(root, root / args.output_dir)
    output_zip = safe_output(root, root / args.output_zip, ".zip")

    require_file(tdsx, "3DSX")
    require_file(smdh, "SMDH")
    require_file(install_guide, "installation guide")
    languages = sorted(lang_dir.glob("*.ini"))
    if not languages:
        raise SystemExit(f"no language files found in {lang_dir}")
    if cia is not None:
        require_file(cia, "CIA")
    if baserom is not None:
        require_file(baserom, "baserom")

    clear_output_tree(output_dir)
    if output_zip.exists():
        try:
            output_zip.unlink()
        except OSError as exc:
            raise SystemExit(f"could not replace distribution zip (is it open?): {output_zip}") from exc

    app_dir = output_dir / "3ds" / "sm64coopdx"
    logs_dir = app_dir / "logs"
    lang_output = app_dir / "lang"
    logs_dir.mkdir(parents=True, exist_ok=True)
    lang_output.mkdir(parents=True, exist_ok=True)

    shutil.copy2(tdsx, app_dir / "sm64coopdx.3dsx")
    shutil.copy2(smdh, app_dir / "sm64coopdx.smdh")
    if cia is not None:
        shutil.copy2(cia, app_dir / "sm64coopdx.cia")
    if baserom is not None:
        dest_rom = app_dir / "baserom.us.z64"
        try:
            shutil.copy2(baserom, dest_rom)
        except OSError:
            if not dest_rom.is_file() or dest_rom.stat().st_size == 0:
                raise
            print(f"warning: left existing locked baserom in place: {dest_rom}")
    for language in languages:
        shutil.copy2(language, lang_output / language.name)
    shutil.copy2(install_guide, output_dir / "INSTALL.md")
    (logs_dir / "README.txt").write_text(
        "SM64CoopDX writes New 3DS diagnostics under sdmc:/3ds/sm64coopdx/logs/.\n"
        "\n"
        "  runtime.log   - boot, ROM, graphics, and network messages\n"
        "  coopnet.log   - CoopNet multiplayer session log (CoopNet builds only)\n",
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

    print(f"Packaged 3DSX: {tdsx.stat().st_size} bytes, SHA-256 {sha256(tdsx)}")
    if cia is not None:
        print(f"Packaged CIA: {cia.stat().st_size} bytes, SHA-256 {sha256(cia)}")
    print(f"Packaged languages: {len(languages)}")
    if baserom is not None:
        packaged_rom = app_dir / "baserom.us.z64"
        print(f"Packaged baserom: {packaged_rom.stat().st_size} bytes, SHA-256 {sha256(packaged_rom)}")
    print(f"Distribution folder: {output_dir}")
    print(f"Distribution ZIP: {output_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
