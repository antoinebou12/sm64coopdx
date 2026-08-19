#!/usr/bin/env python3
"""Run the Switch port's unit tests.

Everything here is asset-free and host-native: no baserom, no devkitA64, no
console. Usage:

    python3 tools/switch/tests/run_tests.py
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TESTS = ROOT / "tools" / "switch" / "tests"
BUILD = ROOT / "build" / "switch-tests"

C_TESTS = {
    "test_socket_ldn_util": ["src/pc/network/socket/socket_ldn_util.c"],
}

CFLAGS = [
    "-std=gnu11",
    "-O1",
    "-Wall",
    "-Wextra",
    "-Werror",
    f"-I{ROOT / 'src'}",
]


def run_c_tests() -> bool:
    cc = os.environ.get("CC")
    if cc is None:
        for candidate in ("cc", "gcc", "clang"):
            cc = shutil.which(candidate)
            if cc is not None:
                break
    if cc is None:
        print("no host C compiler found (set CC)", file=sys.stderr)
        return False

    BUILD.mkdir(parents=True, exist_ok=True)
    ok = True
    for name, sources in C_TESTS.items():
        binary = BUILD / (name + (".exe" if os.name == "nt" else ""))
        cmd = [cc, *CFLAGS, str(TESTS / f"{name}.c")]
        cmd += [str(ROOT / s) for s in sources]
        cmd += ["-o", str(binary)]
        print(f"[cc] {name}")
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            print(f"{name}: compilation failed", file=sys.stderr)
            ok = False
            continue
        if subprocess.run([str(binary)], cwd=ROOT).returncode != 0:
            print(f"{name}: tests failed", file=sys.stderr)
            ok = False
    return ok


def run_python_tests() -> bool:
    loader = unittest.TestLoader()
    suite = loader.discover(str(TESTS), pattern="test_*.py", top_level_dir=str(TESTS))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return result.wasSuccessful()


def main() -> int:
    c_ok = run_c_tests()
    py_ok = run_python_tests()
    if c_ok and py_ok:
        print("\nSwitch unit tests passed.")
        return 0
    print("\nSwitch unit tests FAILED.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
