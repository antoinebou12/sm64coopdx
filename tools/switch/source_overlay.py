#!/usr/bin/env python3
"""Generate small Switch-only source overlays without forking upstream files.

Every transformation is an exact, single replacement. If upstream changes the
expected code, generation fails and forces the Switch port to be reviewed.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one source match, found {count}")
    return text.replace(old, new, 1)


def overlay_mario(text: str) -> str:
    text = replace_once(
        text,
        '#include "pc/network/network.h"\n',
        '#include "pc/network/network.h"\n#include "game/local_multiplayer.h"\n',
        "mario include local multiplayer",
    )

    text = replace_once(
        text,
        "    // don't update remote inputs\n    if (m->playerIndex != 0) { return; }\n",
        "    // Only physical players on this console own controller input.\n"
        "    if (!local_multiplayer_is_active_player(m->playerIndex)) { return; }\n",
        "mario button local ownership",
    )

    text = replace_once(
        text,
        "    // don't update remote inputs past this point\n"
        "    if ((sCurrPlayMode == PLAY_MODE_PAUSED) || m->playerIndex != 0) { return; }\n",
        "    // Remote Marios are network-driven, local split-screen Marios are not.\n"
        "    if ((sCurrPlayMode == PLAY_MODE_PAUSED) ||\n"
        "        !local_multiplayer_is_active_player(m->playerIndex)) { return; }\n",
        "mario joystick local ownership",
    )

    text = replace_once(
        text,
        "void update_mario_inputs(struct MarioState *m) {\n"
        "    if (!m) { return; }\n"
        "    if (m->playerIndex == 0) { m->input = 0; }\n\n"
        "    u8 localIsPaused = (m->playerIndex == 0) && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0);\n",
        "void update_mario_inputs(struct MarioState *m) {\n"
        "    if (!m) { return; }\n"
        "    const bool isLocalPlayer = local_multiplayer_is_active_player(m->playerIndex);\n"
        "    if (isLocalPlayer) { m->input = 0; }\n\n"
        "    u8 localIsPaused = isLocalPlayer && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0);\n",
        "mario input local state",
    )

    text = replace_once(
        text,
        "    // prevent any inputs when paused\n"
        "    if ((m->playerIndex == 0) && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0)) {\n",
        "    // Pause/freeze applies to every locally controlled Mario.\n"
        "    if (isLocalPlayer && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0)) {\n",
        "mario pause local state",
    )

    text = replace_once(
        text,
        "    if (m->playerIndex == 0) {\n"
        "        if (!localIsPaused && (gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {\n",
        "    if (isLocalPlayer) {\n"
        "        if (m->playerIndex == 0 && !localIsPaused && (gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {\n",
        "mario local post-input block",
    )

    text = replace_once(
        text,
        "        if (m->heldObj != NULL) {\n"
        "            m->heldObj->heldByPlayerIndex = 0;\n"
        "        }\n",
        "        if (m->heldObj != NULL) {\n"
        "            m->heldObj->heldByPlayerIndex = m->playerIndex;\n"
        "        }\n",
        "mario held object local index",
    )

    text = replace_once(
        text,
        "        if (m->health < 0x100) {\n"
        "            if (m != &gMarioStates[0]) {\n"
        "                // never kill remote marios\n"
        "                m->health = 0x100;\n"
        "            } else {\n"
        "                m->health = 0xFF;\n"
        "            }\n"
        "        }\n",
        "        if (m->health < 0x100) {\n"
        "            if (!local_multiplayer_is_active_player(m->playerIndex)) {\n"
        "                // Remote Marios are authoritative on their own console.\n"
        "                m->health = 0x100;\n"
        "            } else {\n"
        "                m->health = 0xFF;\n"
        "            }\n"
        "        }\n",
        "mario local death authority",
    )

    text = replace_once(
        text,
        "    // hide inactive players\n"
        "    struct NetworkPlayer *np = &gNetworkPlayers[gMarioState->playerIndex];\n"
        "    if (gMarioState->playerIndex != 0) {\n",
        "    // Hide only remote/inactive peers. Local split-screen Marios are rendered locally.\n"
        "    struct NetworkPlayer *np = &gNetworkPlayers[gMarioState->playerIndex];\n"
        "    if (!local_multiplayer_is_active_player(gMarioState->playerIndex)) {\n",
        "mario remote visibility block",
    )

    text = replace_once(
        text,
        "        // don't update mario when in a cutscene\n"
        "        if (gMarioState->playerIndex == 0) {\n",
        "        // Shared pause/dialog state freezes all Marios controlled on this console.\n"
        "        if (local_multiplayer_is_active_player(gMarioState->playerIndex)) {\n",
        "mario local freeze block",
    )

    text = replace_once(
        text,
        "        // drop held object if someone else is holding it\n"
        "        if (gMarioState->playerIndex == 0 && gMarioState->heldObj != NULL) {\n"
        "            u8 inCutscene = ((gMarioState->action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE);\n"
        "            if (!inCutscene && gMarioState->heldObj->heldByPlayerIndex != 0) {\n",
        "        // Drop a local player's object only when another player actually owns it.\n"
        "        if (local_multiplayer_is_active_player(gMarioState->playerIndex) && gMarioState->heldObj != NULL) {\n"
        "            u8 inCutscene = ((gMarioState->action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE);\n"
        "            if (!inCutscene && gMarioState->heldObj->heldByPlayerIndex != gMarioState->playerIndex) {\n",
        "mario held object ownership check",
    )

    text = replace_once(
        text,
        "        // Make remote players disappear when they enter a painting\n"
        "        // should use same logic as in get_painting_warp_node\n"
        "        if (gMarioState->playerIndex != 0 && gCurrentArea->paintingWarpNodes != NULL) {\n",
        "        // Remote peers disappear through paintings; local split-screen players stay authoritative here.\n"
        "        // should use same logic as in get_painting_warp_node\n"
        "        if (!local_multiplayer_is_active_player(gMarioState->playerIndex) && gCurrentArea->paintingWarpNodes != NULL) {\n",
        "mario painting remote behavior",
    )

    text = replace_once(
        text,
        "    // force all other players to be invisible by default\n"
        "    if (playerIndex != 0) {\n",
        "    // Only remote slots start invisible. Connected local controllers are immediately visible.\n"
        "    if (!local_multiplayer_is_active_player(playerIndex)) {\n",
        "mario initial local visibility",
    )

    text = replace_once(
        text,
        "void set_mario_particle_flags(struct MarioState* m, u32 flags, u8 clear) {\n"
        "    if (!m) { return; }\n"
        "    if (m->playerIndex != 0) {\n"
        "        return;\n"
        "    }\n",
        "void set_mario_particle_flags(struct MarioState* m, u32 flags, u8 clear) {\n"
        "    if (!m) { return; }\n"
        "    if (!local_multiplayer_is_active_player(m->playerIndex)) {\n"
        "        return;\n"
        "    }\n",
        "mario local particle authority",
    )

    text = replace_once(
        text,
        "    u8 teleportFade = (m->flags & MARIO_TELEPORTING) || (gMarioState->playerIndex != 0 && np->fadeOpacity < 32);\n",
        "    u8 teleportFade = (m->flags & MARIO_TELEPORTING) ||\n"
        "        (!local_multiplayer_is_active_player(gMarioState->playerIndex) && np->fadeOpacity < 32);\n",
        "mario local teleport fade",
    )

    return text


def overlay_network(text: str) -> str:
    text = replace_once(
        text,
        '#include "socket/socket.h"\n',
        '#include "socket/socket.h"\n#ifdef __SWITCH__\n#include "socket/socket_ldn.h"\n#endif\n',
        "network include LDN",
    )

    text = replace_once(
        text,
        "    switch (nsType) {\n"
        "        case NS_SOCKET:  gNetworkSystem = &gNetworkSystemSocket; break;\n"
        "#ifdef COOPNET\n"
        "        case NS_COOPNET: gNetworkSystem = &gNetworkSystemCoopNet; break;\n"
        "#endif\n"
        "        default: gNetworkSystem = &gNetworkSystemSocket; LOG_ERROR(\"Unknown network system: %d\", nsType); break;\n"
        "    }\n",
        "    switch (nsType) {\n"
        "        case NS_SOCKET:  gNetworkSystem = &gNetworkSystemSocket; break;\n"
        "#ifdef COOPNET\n"
        "        case NS_COOPNET: gNetworkSystem = &gNetworkSystemCoopNet; break;\n"
        "#endif\n"
        "#ifdef __SWITCH__\n"
        "        case NS_LDN:     gNetworkSystem = &gNetworkSystemLdn; break;\n"
        "#endif\n"
        "        default: gNetworkSystem = &gNetworkSystemSocket; LOG_ERROR(\"Unknown network system: %d\", nsType); break;\n"
        "    }\n",
        "network select LDN",
    )

    text = replace_once(
        text,
        "#ifdef COOPNET\n"
        "    sNetworkReconnectType = (gNetworkSystem == &gNetworkSystemCoopNet)\n"
        "                          ? NS_COOPNET\n"
        "                          : NS_SOCKET;\n"
        "#else\n"
        "    sNetworkReconnectType = NS_SOCKET;\n"
        "#endif\n",
        "#ifdef __SWITCH__\n"
        "    if (gNetworkSystem == &gNetworkSystemLdn) {\n"
        "        sNetworkReconnectType = NS_LDN;\n"
        "    } else\n"
        "#endif\n"
        "#ifdef COOPNET\n"
        "    if (gNetworkSystem == &gNetworkSystemCoopNet) {\n"
        "        sNetworkReconnectType = NS_COOPNET;\n"
        "    } else {\n"
        "        sNetworkReconnectType = NS_SOCKET;\n"
        "    }\n"
        "#else\n"
        "    {\n"
        "        sNetworkReconnectType = NS_SOCKET;\n"
        "    }\n"
        "#endif\n",
        "network remember LDN reconnect type",
    )

    text = replace_once(
        text,
        "    if (sNetworkReconnectType == NS_SOCKET) {\n"
        "        network_set_system(NS_SOCKET);\n"
        "    } else if (sNetworkReconnectType == NS_COOPNET) {\n"
        "        network_set_system(NS_COOPNET);\n"
        "    }\n",
        "    if (sNetworkReconnectType == NS_SOCKET) {\n"
        "        network_set_system(NS_SOCKET);\n"
        "    } else if (sNetworkReconnectType == NS_COOPNET) {\n"
        "        network_set_system(NS_COOPNET);\n"
        "#ifdef __SWITCH__\n"
        "    } else if (sNetworkReconnectType == NS_LDN) {\n"
        "        network_set_system(NS_LDN);\n"
        "#endif\n"
        "    }\n",
        "network restore LDN reconnect type",
    )

    return text


OVERLAYS = {
    "mario": overlay_mario,
    "network": overlay_network,
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
