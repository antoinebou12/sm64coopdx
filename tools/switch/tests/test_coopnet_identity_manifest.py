from __future__ import annotations

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MODULE_PATH = ROOT / "tools" / "switch" / "coopnet_identity.py"
SPEC = importlib.util.spec_from_file_location("coopnet_identity", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
identity = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(identity)


def synthetic_nro(path: Path) -> bytes:
    nro_size = 0x100
    image = bytearray(nro_size)
    image[0x10:0x14] = b"NRO0"
    struct.pack_into("<I", image, 0x18, nro_size)
    icon = b"\xff\xd8synthetic-switch-icon\xff\xd9"
    aset = struct.pack("<4sI6Q", b"ASET", 0, 56, len(icon), 0, 0, 0, 0)
    data = bytes(image) + aset + icon
    path.write_bytes(data)
    return data


class CoopNetIdentityManifestTests(unittest.TestCase):
    def test_known_libstdcxx_hash_vectors(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture.bin"
            vectors = {
                b"a": 4993892634952068459,
                b"1234567": 7888703982793837558,
                b"12345678": 10113836418918046885,
                b"123456789": 12315982496267331008,
            }
            for payload, expected in vectors.items():
                path.write_bytes(payload)
                self.assertEqual(identity.murmur64a_file(path), (expected, len(payload)))

    def test_chunk_boundary_and_large_fixture_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "large.bin"
            payload = bytes((i * 17 + 3) & 0xFF for i in range(2 * 1024 * 1024 + 9))
            path.write_bytes(payload)
            first = identity.murmur64a_file(path)
            second = identity.murmur64a_file(path)
            self.assertEqual(first, second)
            self.assertEqual(first[1], len(payload))
            self.assertNotEqual(first[0], 0)

    def test_nro_assets_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            nro = directory / "sm64coopdx.nro"
            output = directory / "switch-coopnet-identity.txt"
            data = synthetic_nro(nro)

            assets = identity.inspect_nro(nro)
            self.assertEqual(assets.nro_size, 0x100)
            self.assertEqual(assets.icon_offset, 56)
            self.assertGreater(assets.icon_size, 0)

            fingerprint = identity.write_manifest(
                nro,
                output,
                ROOT,
                "9d9b3dd4e87dba2fa3ca542ae32b73f43df32b0e",
                "test-switch",
            )
            manifest = output.read_text(encoding="utf-8")
            self.assertEqual(fingerprint, identity.murmur64a_file(nro)[0])
            self.assertIn(f"NRO size: {len(data)}", manifest)
            self.assertIn(f"CoopNet fingerprint decimal: {fingerprint}", manifest)
            self.assertIn(f"UINT64_C({fingerprint})", manifest)
            self.assertIn("Embedded icon present: yes", manifest)

    def test_invalid_or_iconless_nro_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.nro"
            path.write_bytes(b"not an nro")
            with self.assertRaises(ValueError):
                identity.inspect_nro(path)


if __name__ == "__main__":
    unittest.main()
