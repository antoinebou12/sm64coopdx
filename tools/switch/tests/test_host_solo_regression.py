#!/usr/bin/env python3
"""Source-level regression checks for Switch Host/Solo startup.

These are deliberately asset-free so the portability job can catch accidental
reintroduction of the fake socket-free server or per-file durable SD commits
without requiring a baserom or physical Switch.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> None:
    socket_c = read("src/pc/network/socket/socket.c")
    host_c = read("src/pc/djui/djui_panel_host.c")
    host_message_c = read("src/pc/djui/djui_panel_host_message.c")
    crash_log_c = read("src/pc/platform/switch/switch_crash_log.c")

    assert "sSwitchLocalOnly" not in socket_c
    assert "local-only host initialized without UDP socket" not in socket_c
    assert '"PLAY SOLO"' in host_c
    assert '"PLAY SOLO (NO NETWORK)"' not in host_c

    assert "if (!network_init(NT_SERVER, reconnecting))" in host_message_c
    assert 'switch_crash_log_checkpoint("host: network init failed")' in host_message_c
    assert 'switch_crash_log_checkpoint("host: transition started")' in host_message_c

    assert "switch_crash_log_checkpoint_is_noisy" in crash_log_c
    assert 'strncmp(checkpoint, "remote file cache path ", 23)' in crash_log_c
    assert 'strncmp(checkpoint, "remote file timestamp ", 22)' in crash_log_c
    assert "SWITCH_VERBOSE_MOD_CHECKPOINTS" in crash_log_c

    print("Switch Host/Solo regression checks passed")


if __name__ == "__main__":
    main()
