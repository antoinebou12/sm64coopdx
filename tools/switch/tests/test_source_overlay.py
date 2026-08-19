#!/usr/bin/env python3
"""Tests for the Horizon source overlay generator.

The overlays are anchored to exact upstream text. When an upstream merge
reflows one of those anchors the overlay stops applying to the code it was
written for, and the Switch build quietly loses the patch. These tests run
every overlay against the real source file it targets, so drift fails here
instead of on hardware.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "switch"))

import source_overlay  # noqa: E402

# The source each overlay is applied to, mirroring Makefile.switch-game.
OVERLAY_SOURCES = {
    "pc_main": "src/pc/pc_main.c",
    "platform": "src/pc/platform.c",
    "controller_bind": "src/pc/controller/controller_bind_mapping.c",
    "djui_controls": "src/pc/djui/djui_panel_controls.c",
    "loading": "src/pc/loading.c",
    "network": "src/pc/network/network.c",
    "djui_host": "src/pc/djui/djui_panel_host.c",
}

# Text each overlay is responsible for introducing.
OVERLAY_MARKERS = {
    "pc_main": "pc/platform/switch/switch_platform.h",
    "platform": "sdmc:/switch/sm64coopdx",
    "controller_bind": "#include <SDL2/SDL.h>",
    "djui_controls": "Nintendo Switch",
    "loading": "Copy baserom.us.z64 to sdmc:/switch/sm64coopdx",
    "network": "gNetworkSystemLdn",
    "djui_host": "configNetworkSystem = NS_SOCKET",
}


class TestOverlayCoverage(unittest.TestCase):
    def test_every_overlay_has_a_known_source(self):
        self.assertEqual(set(source_overlay.OVERLAYS), set(OVERLAY_SOURCES))

    def test_every_overlay_has_a_marker(self):
        self.assertEqual(set(source_overlay.OVERLAYS), set(OVERLAY_MARKERS))


class TestOverlaysApplyToRealSources(unittest.TestCase):
    def test_each_overlay_changes_its_source(self):
        for mode, relative in OVERLAY_SOURCES.items():
            with self.subTest(overlay=mode):
                original = (ROOT / relative).read_text(encoding="utf-8")
                patched = source_overlay.OVERLAYS[mode](original)
                self.assertNotEqual(
                    original,
                    patched,
                    f"overlay {mode!r} did not modify {relative}: its anchor has drifted",
                )

    def test_each_overlay_introduces_its_marker(self):
        for mode, relative in OVERLAY_SOURCES.items():
            with self.subTest(overlay=mode):
                original = (ROOT / relative).read_text(encoding="utf-8")
                patched = source_overlay.OVERLAYS[mode](original)
                self.assertTrue(
                    OVERLAY_MARKERS[mode] in patched,
                    f"overlay {mode!r} no longer introduces {OVERLAY_MARKERS[mode]!r}",
                )

    def test_each_overlay_stays_a_narrow_patch(self):
        # Overlays exist so the Switch build tracks upstream instead of forking
        # whole files. Some replace text in place and some remove lines, so the
        # bound is on how far the line count moves in either direction.
        for mode, relative in OVERLAY_SOURCES.items():
            with self.subTest(overlay=mode):
                original = (ROOT / relative).read_text(encoding="utf-8")
                patched = source_overlay.OVERLAYS[mode](original)
                delta = abs(len(patched.splitlines()) - len(original.splitlines()))
                self.assertLess(delta, 120, f"overlay {mode!r} is no longer a narrow patch")

    def test_each_overlay_rejects_a_source_it_does_not_match(self):
        # The fail-loud contract: a missing anchor must stop the build.
        # replace_once/replace_exact_count raise RuntimeError; the djui_controls
        # overlay slices on str.index and raises ValueError.
        for mode in OVERLAY_SOURCES:
            with self.subTest(overlay=mode):
                with self.assertRaises((RuntimeError, ValueError)):
                    source_overlay.OVERLAYS[mode]("int main(void) { return 0; }\n")


class TestReplaceHelpers(unittest.TestCase):
    def test_replace_once_replaces_a_unique_anchor(self):
        self.assertEqual(source_overlay.replace_once("a b c", "b", "B", "label"), "a B c")

    def test_replace_once_rejects_a_missing_anchor(self):
        with self.assertRaises(RuntimeError):
            source_overlay.replace_once("a b c", "zzz", "B", "label")

    def test_replace_once_rejects_an_ambiguous_anchor(self):
        with self.assertRaises(RuntimeError):
            source_overlay.replace_once("b b", "b", "B", "label")

    def test_replace_exact_count_requires_the_stated_count(self):
        self.assertEqual(
            source_overlay.replace_exact_count("b b", "b", "B", 2, "label"), "B B"
        )
        with self.assertRaises(RuntimeError):
            source_overlay.replace_exact_count("b b", "b", "B", 3, "label")


if __name__ == "__main__":
    unittest.main()
