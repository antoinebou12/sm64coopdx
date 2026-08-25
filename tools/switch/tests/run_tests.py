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


def native_arg(path: Path) -> str:
    """Use slash-separated absolute paths accepted by MSYS and native compilers."""
    return path.resolve().as_posix()

C_TESTS = {
    "test_coopnet_join_recovery": ["src/pc/network/coopnet/coopnet_join_recovery.c"],
    "test_socket_ldn_util": ["src/pc/network/socket/socket_ldn_util.c"],
}

CFLAGS = [
    "-std=gnu11",
    "-O1",
    "-Wall",
    "-Wextra",
    "-Werror",
    f"-I{native_arg(ROOT / 'src')}",
]

CXX_TESTS = {
    "test_coopnet_switch_identity": ["tools/switch/coopnet_switch_identity.cpp"],
}

CXXFLAGS = [
    "-std=c++17",
    "-O1",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-pthread",
    f"-I{native_arg(ROOT / 'tools' / 'switch')}",
]


def compiler_works(cc: str) -> bool:
    """A compiler on PATH is not necessarily usable.

    A devkitPro or other cross toolchain can shadow `cc`/`gcc` while being
    unable to build a host binary at all, so probe before committing to one.
    """
    probe = BUILD / "probe.c"
    probe.write_text("#include <stdio.h>\nint main(void) { return 0; }\n", encoding="utf-8")
    binary = BUILD / ("probe.exe" if os.name == "nt" else "probe.out")
    result = subprocess.run(
        [cc, str(probe), "-o", str(binary)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def find_working_compiler() -> str | None:
    BUILD.mkdir(parents=True, exist_ok=True)

    override = os.environ.get("CC")
    if override:
        # An explicit CC is respected even if the probe fails, so its real
        # error is reported against the actual test build.
        return override

    for candidate in ("cc", "gcc", "clang"):
        path = shutil.which(candidate)
        if path is None:
            continue
        if compiler_works(path):
            return path
        print(f"[cc] skipping {path}: cannot build a host binary")
    return None


def cxx_compiler_works(cxx: str) -> bool:
    probe = BUILD / "probe.cpp"
    probe.write_text("#include <iostream>\nint main() { return 0; }\n", encoding="utf-8")
    binary = BUILD / ("probe-cxx.exe" if os.name == "nt" else "probe-cxx.out")
    result = subprocess.run(
        [cxx, "-std=c++17", str(probe), "-o", str(binary)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def find_working_cxx_compiler() -> str | None:
    BUILD.mkdir(parents=True, exist_ok=True)
    override = os.environ.get("CXX")
    if override:
        return override
    for candidate in ("c++", "g++", "clang++"):
        path = shutil.which(candidate)
        if path is not None and cxx_compiler_works(path):
            return path
    return None


def run_c_tests() -> bool:
    cc = find_working_compiler()
    if cc is None:
        print("no working host C compiler found (set CC)", file=sys.stderr)
        return False

    BUILD.mkdir(parents=True, exist_ok=True)
    ok = True
    for name, sources in C_TESTS.items():
        binary = BUILD / (name + (".exe" if os.name == "nt" else ""))
        cmd = [cc, *CFLAGS, native_arg(TESTS / f"{name}.c")]
        cmd += [native_arg(ROOT / s) for s in sources]
        cmd += ["-o", native_arg(binary)]
        print(f"[cc] {name}")
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            print(f"{name}: compilation failed", file=sys.stderr)
            ok = False
            continue
        if subprocess.run([native_arg(binary)], cwd=ROOT).returncode != 0:
            print(f"{name}: tests failed", file=sys.stderr)
            ok = False
    return ok


def run_cxx_tests() -> bool:
    cxx = find_working_cxx_compiler()
    if cxx is None:
        print("no working host C++ compiler found (set CXX)", file=sys.stderr)
        return False

    ok = True
    for name, sources in CXX_TESTS.items():
        binary = BUILD / (name + (".exe" if os.name == "nt" else ""))
        cmd = [cxx, *CXXFLAGS, native_arg(TESTS / f"{name}.cpp")]
        cmd += [native_arg(ROOT / source) for source in sources]
        cmd += ["-o", native_arg(binary)]
        print(f"[cxx] {name}")
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            print(f"{name}: compilation failed", file=sys.stderr)
            ok = False
            continue
        if subprocess.run([native_arg(binary)], cwd=ROOT).returncode != 0:
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
    cxx_ok = run_cxx_tests()
    py_ok = run_python_tests()
    if c_ok and cxx_ok and py_ok:
        print("\nSwitch unit tests passed.")
        return 0
    print("\nSwitch unit tests FAILED.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
