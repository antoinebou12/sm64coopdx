from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class SwitchJoinCharacterPromptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.join_message = (ROOT / "src/pc/djui/djui_panel_join_message.c").read_text(
            encoding="utf-8"
        )
        cls.download = (ROOT / "src/pc/network/packets/packet_download.c").read_text(
            encoding="utf-8"
        )
        cls.config = (ROOT / "src/pc/configfile.c").read_text(encoding="utf-8")
        cls.player = (ROOT / "src/pc/djui/djui_panel_player.c").read_text(
            encoding="utf-8"
        )

    def test_selection_is_persisted_and_normal_player_menu_counts(self) -> None:
        self.assertIn('"coop_player_model_selected"', self.config)
        self.assertIn("configPlayerModelSelected = true;", self.player)
        self.assertIn("configfile_save(configfile_name());", self.join_message)

    def test_prompt_is_switch_coopnet_only_and_precedes_game_join(self) -> None:
        self.assertIn("#if defined(__SWITCH__) && defined(COOPNET)", self.join_message)
        self.assertIn("gNetworkSystem == &gNetworkSystemCoopNet", self.join_message)
        self.assertIn("!configPlayerModelSelected", self.join_message)
        self.assertIn("djui_panel_join_message_ready_to_join();", self.download)

    def test_confirm_is_single_shot_and_late_callbacks_are_ignored(self) -> None:
        guard = self.join_message.index(
            "if (!sCharacterPromptActive || gNetworkType != NT_CLIENT"
        )
        sent_guard = self.join_message.index("gNetworkSentJoin", guard)
        send = self.join_message.index("network_send_join_request();", sent_guard)
        self.assertLess(guard, sent_guard)
        self.assertLess(sent_guard, send)

    def test_cancel_clears_prompt_before_network_shutdown(self) -> None:
        cancel = self.join_message.index("djui_panel_join_character_cancel")
        cleared = self.join_message.index("sCharacterPromptActive = false;", cancel)
        shutdown = self.join_message.index("network_shutdown(true", cleared)
        self.assertLess(cleared, shutdown)


if __name__ == "__main__":
    unittest.main()
