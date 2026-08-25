from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PACKAGER = ROOT / "tools" / "switch" / "package_dist.py"


class SwitchDistributionTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> list[str]:
        nro = root / "sm64coopdx.nro"
        image = bytearray(64)
        image[16:20] = b"NRO0"
        nro.write_bytes(image)
        digest = hashlib.sha256(image).hexdigest()

        (root / "icon.jpg").write_bytes(b"synthetic icon")
        (root / "lang").mkdir()
        (root / "lang" / "English.ini").write_text("[lang]\n", encoding="utf-8")
        (root / "INSTALL.md").write_text("# Install\n", encoding="utf-8")
        (root / "identity.txt").write_text(
            "\n".join(
                (
                    f"NRO size: {len(image)}",
                    f"NRO SHA-256: {digest}",
                    "Embedded icon present: yes",
                )
            )
            + "\n",
            encoding="utf-8",
        )

        return [
            sys.executable,
            str(PACKAGER),
            "--root",
            str(root),
            "--nro",
            "sm64coopdx.nro",
            "--icon",
            "icon.jpg",
            "--lang-dir",
            "lang",
            "--identity",
            "identity.txt",
            "--install-guide",
            "INSTALL.md",
            "--output-dir",
            "dist/sm64coopdx-switch-test",
            "--output-zip",
            "dist/sm64coopdx-switch-test.zip",
        ]

    def test_creates_valid_rom_free_zip(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            result = subprocess.run(self.make_fixture(root), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

            archive_path = root / "dist" / "sm64coopdx-switch-test.zip"
            with zipfile.ZipFile(archive_path) as archive:
                self.assertIsNone(archive.testzip())
                names = set(archive.namelist())
            self.assertIn("switch/sm64coopdx/sm64coopdx.nro", names)
            self.assertIn("switch/sm64coopdx/lang/English.ini", names)
            self.assertIn("SHA256SUMS.txt", names)
            self.assertFalse(any("baserom" in name.lower() for name in names))

    def test_rejects_manifest_for_a_different_nro(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            command = self.make_fixture(root)
            identity = root / "identity.txt"
            identity.write_text(
                identity.read_text(encoding="utf-8").replace("NRO size: 64", "NRO size: 63"),
                encoding="utf-8",
            )
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("size does not match", result.stderr)


if __name__ == "__main__":
    unittest.main()
