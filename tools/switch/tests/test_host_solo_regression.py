#!/usr/bin/env python3
"""Source-level regression checks for Switch Host/Solo startup."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class HostSoloRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.socket_c = read("src/pc/network/socket/socket.c")
        cls.host_c = read("src/pc/djui/djui_panel_host.c")
        cls.host_message_c = read("src/pc/djui/djui_panel_host_message.c")
        cls.crash_log_c = read("src/pc/platform/switch/switch_crash_log.c")

    def test_solo_uses_normal_socket_server_lifecycle(self) -> None:
        self.assertNotIn("sSwitchLocalOnly", self.socket_c)
        self.assertNotIn("local-only host initialized without UDP socket", self.socket_c)
        self.assertIn('"PLAY SOLO"', self.host_c)
        self.assertNotIn('"PLAY SOLO (NO NETWORK)"', self.host_c)

    def test_host_init_failure_does_not_continue_transition(self) -> None:
        self.assertIn("if (!network_init(NT_SERVER, reconnecting))", self.host_message_c)
        self.assertIn('switch_crash_log_checkpoint("host: network init failed")', self.host_message_c)
        self.assertIn("network_shutdown(false, false, true, false);", self.host_message_c)
        self.assertIn('switch_crash_log_checkpoint("host: transition started")', self.host_message_c)

    def test_per_file_mod_checkpoints_are_not_durably_committed_by_default(self) -> None:
        self.assertIn("switch_crash_log_checkpoint_is_noisy", self.crash_log_c)
        self.assertIn('strncmp(checkpoint, "remote file cache path ", 23)', self.crash_log_c)
        self.assertIn('strncmp(checkpoint, "remote file timestamp ", 22)', self.crash_log_c)
        self.assertIn("SWITCH_VERBOSE_MOD_CHECKPOINTS", self.crash_log_c)


if __name__ == "__main__":
    unittest.main()
