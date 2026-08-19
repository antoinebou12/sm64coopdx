#include "local_multiplayer.h"

#include <stddef.h>

static uint8_t sLocalPlayerCount = 1;
static uint8_t sCurrentLocalPlayer = 0;
static LocalSplitLayout sRequestedLayout = LOCAL_SPLIT_AUTO;
static LocalPlayerSlot sLocalPlayers[LOCAL_MULTIPLAYER_MAX_PLAYERS];

static LocalSplitLayout resolved_layout(void) {
    if (sRequestedLayout != LOCAL_SPLIT_AUTO) {
        if (sLocalPlayerCount == 1) {
            return LOCAL_SPLIT_SINGLE;
        }
        if (sLocalPlayerCount == 2 &&
            (sRequestedLayout == LOCAL_SPLIT_TWO_HORIZONTAL ||
             sRequestedLayout == LOCAL_SPLIT_TWO_VERTICAL)) {
            return sRequestedLayout;
        }
        if (sLocalPlayerCount == 3 && sRequestedLayout == LOCAL_SPLIT_THREE) {
            return sRequestedLayout;
        }
        if (sLocalPlayerCount == 4 && sRequestedLayout == LOCAL_SPLIT_FOUR) {
            return sRequestedLayout;
        }
    }

    switch (sLocalPlayerCount) {
        case 1: return LOCAL_SPLIT_SINGLE;
        case 2: return LOCAL_SPLIT_TWO_HORIZONTAL;
        case 3: return LOCAL_SPLIT_THREE;
        default: return LOCAL_SPLIT_FOUR;
    }
}

void local_multiplayer_reset(void) {
    sLocalPlayerCount = 1;
    sCurrentLocalPlayer = 0;
    sRequestedLayout = LOCAL_SPLIT_AUTO;

    for (uint8_t i = 0; i < LOCAL_MULTIPLAYER_MAX_PLAYERS; i++) {
        sLocalPlayers[i].active = (i == 0);
        sLocalPlayers[i].controller_slot = i;
        sLocalPlayers[i].network_index = LOCAL_MULTIPLAYER_INVALID_NETWORK_INDEX;
    }
}

bool local_multiplayer_set_player_count(uint8_t count) {
    if (count < 1 || count > LOCAL_MULTIPLAYER_MAX_PLAYERS) {
        return false;
    }

    sLocalPlayerCount = count;
    if (sCurrentLocalPlayer >= count) {
        sCurrentLocalPlayer = 0;
    }

    for (uint8_t i = 0; i < LOCAL_MULTIPLAYER_MAX_PLAYERS; i++) {
        sLocalPlayers[i].active = i < count;
        if (!sLocalPlayers[i].active) {
            sLocalPlayers[i].network_index = LOCAL_MULTIPLAYER_INVALID_NETWORK_INDEX;
        }
    }
    return true;
}

uint8_t local_multiplayer_player_count(void) {
    return sLocalPlayerCount;
}

uint8_t local_multiplayer_sync_controller_count(uint8_t connected_count, bool allow_multiple) {
    uint8_t count = connected_count;
    if (!allow_multiple || count == 0) {
        count = 1;
    }
    if (count > LOCAL_MULTIPLAYER_MAX_PLAYERS) {
        count = LOCAL_MULTIPLAYER_MAX_PLAYERS;
    }

    local_multiplayer_set_player_count(count);
    for (uint8_t i = 0; i < count; i++) {
        sLocalPlayers[i].controller_slot = i;
    }
    return sLocalPlayerCount;
}

bool local_multiplayer_is_active_player(uint8_t player) {
    return player < sLocalPlayerCount && sLocalPlayers[player].active;
}

void local_multiplayer_set_layout(LocalSplitLayout layout) {
    if (layout < LOCAL_SPLIT_AUTO || layout > LOCAL_SPLIT_FOUR) {
        return;
    }
    sRequestedLayout = layout;
}

LocalSplitLayout local_multiplayer_layout(void) {
    return resolved_layout();
}

bool local_multiplayer_bind_controller(uint8_t player, uint8_t controller_slot) {
    if (player >= sLocalPlayerCount || controller_slot >= LOCAL_MULTIPLAYER_MAX_PLAYERS) {
        return false;
    }
    sLocalPlayers[player].controller_slot = controller_slot;
    return true;
}

bool local_multiplayer_bind_network_index(uint8_t player, uint8_t network_index) {
    if (player >= sLocalPlayerCount) {
        return false;
    }
    sLocalPlayers[player].network_index = network_index;
    return true;
}

void local_multiplayer_reset_network_bindings(void) {
    for (uint8_t i = 0; i < LOCAL_MULTIPLAYER_MAX_PLAYERS; i++) {
        sLocalPlayers[i].network_index = LOCAL_MULTIPLAYER_INVALID_NETWORK_INDEX;
    }
}

const LocalPlayerSlot *local_multiplayer_slot(uint8_t player) {
    if (player >= sLocalPlayerCount) {
        return NULL;
    }
    return &sLocalPlayers[player];
}

bool local_multiplayer_get_viewport(uint8_t player, LocalViewport *viewport) {
    if (viewport == NULL || player >= sLocalPlayerCount) {
        return false;
    }

    const LocalSplitLayout layout = resolved_layout();
    switch (layout) {
        case LOCAL_SPLIT_SINGLE:
            *viewport = (LocalViewport){ 0.0f, 0.0f, 1.0f, 1.0f };
            return player == 0;

        case LOCAL_SPLIT_TWO_HORIZONTAL:
            *viewport = (LocalViewport){
                0.0f,
                player == 0 ? 0.0f : 0.5f,
                1.0f,
                0.5f,
            };
            return player < 2;

        case LOCAL_SPLIT_TWO_VERTICAL:
            *viewport = (LocalViewport){
                player == 0 ? 0.0f : 0.5f,
                0.0f,
                0.5f,
                1.0f,
            };
            return player < 2;

        case LOCAL_SPLIT_THREE:
            if (player == 0) {
                *viewport = (LocalViewport){ 0.0f, 0.0f, 0.5f, 0.5f };
            } else if (player == 1) {
                *viewport = (LocalViewport){ 0.5f, 0.0f, 0.5f, 0.5f };
            } else {
                *viewport = (LocalViewport){ 0.0f, 0.5f, 1.0f, 0.5f };
            }
            return player < 3;

        case LOCAL_SPLIT_FOUR:
            *viewport = (LocalViewport){
                (player & 1U) ? 0.5f : 0.0f,
                (player & 2U) ? 0.5f : 0.0f,
                0.5f,
                0.5f,
            };
            return player < 4;

        case LOCAL_SPLIT_AUTO:
        default:
            return false;
    }
}

bool local_multiplayer_get_viewport_pixels(uint8_t player, int screen_width, int screen_height,
                                           LocalViewportPixels *viewport) {
    if (viewport == NULL || screen_width <= 0 || screen_height <= 0) {
        return false;
    }

    LocalViewport normalized;
    if (!local_multiplayer_get_viewport(player, &normalized)) {
        return false;
    }

    const int x0 = (int)(normalized.x * screen_width);
    const int y0 = (int)(normalized.y * screen_height);
    const int x1 = (int)((normalized.x + normalized.width) * screen_width);
    const int y1 = (int)((normalized.y + normalized.height) * screen_height);

    viewport->x = x0;
    viewport->y = y0;
    viewport->width = x1 - x0;
    viewport->height = y1 - y0;
    return true;
}

void local_multiplayer_begin_player(uint8_t player) {
    if (local_multiplayer_is_active_player(player)) {
        sCurrentLocalPlayer = player;
    }
}

void local_multiplayer_end_player(void) {
    sCurrentLocalPlayer = 0;
}

uint8_t local_multiplayer_current_player(void) {
    return sCurrentLocalPlayer;
}
