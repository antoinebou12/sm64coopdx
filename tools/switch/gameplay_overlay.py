#!/usr/bin/env python3
"""Generate Switch-only gameplay overlays with exact-match assertions."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one source match, found {count}")
    return text.replace(old, new, 1)


def overlay_obj_behaviors(text: str) -> str:
    text = replace_once(
        text,
        '#include "game/rng_position.h"\n',
        '#include "game/rng_position.h"\n#include "game/local_multiplayer.h"\n',
        "obj behaviors include local multiplayer",
    )

    text = replace_once(
        text,
        "u8 is_player_active(struct MarioState* m) {\n"
        "    if (!m) { return FALSE; }\n"
        "    if (gNetworkType == NT_NONE && m == &gMarioStates[0]) { return TRUE; }\n"
        "    if (m->action == ACT_BUBBLED) { return FALSE; }\n",
        "u8 is_player_active(struct MarioState* m) {\n"
        "    if (!m) { return FALSE; }\n"
        "    if (m->action == ACT_BUBBLED) { return FALSE; }\n"
        "    if (local_multiplayer_is_active_player(m->playerIndex)) { return TRUE; }\n",
        "obj behaviors local active predicate",
    )

    text = replace_once(
        text,
        "u8 is_player_in_local_area(struct MarioState* m) {\n"
        "    if (!m) { return FALSE; }\n"
        "    if (gNetworkType == NT_NONE && m == &gMarioStates[0]) { return TRUE; }\n"
        "    struct NetworkPlayer* np = &gNetworkPlayers[m->playerIndex];\n",
        "u8 is_player_in_local_area(struct MarioState* m) {\n"
        "    if (!m) { return FALSE; }\n"
        "    if (local_multiplayer_is_active_player(m->playerIndex)) { return TRUE; }\n"
        "    struct NetworkPlayer* np = &gNetworkPlayers[m->playerIndex];\n",
        "obj behaviors local area predicate",
    )

    return text


OVERLAYS = {
    "obj_behaviors": overlay_obj_behaviors,
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=OVERLAYS)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    output = OVERLAYS[args.kind](source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
