#!/usr/bin/env python3
"""Tests for the Horizon source invariant checker's own helpers.

strip_comments() decides whether a "this file must not call X" invariant looks
at code or at prose. Getting it wrong either lets a real regression through or
fails the build over a comment, so its behavior is pinned here.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "switch"))

import ci_source_check  # noqa: E402

strip_comments = ci_source_check.strip_comments


class TestStripComments(unittest.TestCase):
    def test_line_comment_is_removed(self):
        self.assertNotIn("socketInitializeDefault", strip_comments("// socketInitializeDefault()\n"))

    def test_code_after_a_line_comment_survives(self):
        stripped = strip_comments("// note\nint x = call();\n")
        self.assertIn("int x = call();", stripped)

    def test_code_before_a_line_comment_survives(self):
        stripped = strip_comments("int x = call(); // note about socketExit()\n")
        self.assertIn("int x = call();", stripped)
        self.assertNotIn("socketExit", stripped)

    def test_block_comment_is_removed(self):
        self.assertNotIn("socketExit", strip_comments("/* socketExit() */ int x;"))

    def test_block_comment_spanning_lines_is_removed(self):
        source = "/*\n * socketInitializeDefault() is owned elsewhere\n */\nint x;\n"
        stripped = strip_comments(source)
        self.assertNotIn("socketInitializeDefault", stripped)
        self.assertIn("int x;", stripped)

    def test_consecutive_block_comments_do_not_swallow_code(self):
        stripped = strip_comments("/* a */ keep_me(); /* b */")
        self.assertIn("keep_me();", stripped)

    def test_real_call_still_survives(self):
        source = "// socketInitializeDefault() is called by the platform\nsocketInitializeDefault();\n"
        self.assertIn("socketInitializeDefault()", strip_comments(source))

    def test_double_slash_inside_a_string_literal_is_also_stripped(self):
        # Known limitation: this is a text filter, not a C lexer, so a "//"
        # inside a string literal truncates the rest of that line. Invariants
        # must not depend on such text - pinned here so a future change to
        # strip_comments is a deliberate one.
        stripped = strip_comments('const char* p = "sdmc://path";')
        self.assertNotIn("path", stripped)
        self.assertIn('const char* p = "sdmc:', stripped)


class TestCheckerIsImportable(unittest.TestCase):
    def test_importing_does_not_run_the_checks(self):
        # The checks read generated overlays that only exist mid-build, so the
        # module must stay import-safe for tooling and tests.
        self.assertTrue(callable(ci_source_check.main))
        self.assertEqual(ci_source_check.failures, [])
        self.assertEqual(ci_source_check.passes, [])


if __name__ == "__main__":
    unittest.main()
