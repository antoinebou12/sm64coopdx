#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOCAL_MULTIPLAYER_MAX_PLAYERS 4
#define LOCAL_MULTIPLAYER_INVALID_NETWORK_INDEX 0xFF

typedef enum LocalSplitLayout {
    LOCAL_SPLIT_AUTO = 0,
    LOCAL_SPLIT_SINGLE,
    LOCAL_SPLIT_TWO_HORIZONTAL,
    LOCAL_SPLIT_TWO_VERTICAL,
    LOCAL_SPLIT_THREE,
    LOCAL_SPLIT_FOUR,
} LocalSplitLayout;

typedef struct LocalViewport {
    float x;
    float y;
    float width;
    float height;
} LocalViewport;

typedef struct LocalViewportPixels {
    int x;
    int y;
    int width;
    int height;
} LocalViewportPixels;

typedef struct LocalPlayerSlot {
    bool active;
    uint8_t controller_slot;
    uint8_t network_index;
} LocalPlayerSlot;

void local_multiplayer_reset(void);
bool local_multiplayer_set_player_count(uint8_t count);
uint8_t local_multiplayer_player_count(void);

void local_multiplayer_set_layout(LocalSplitLayout layout);
LocalSplitLayout local_multiplayer_layout(void);

bool local_multiplayer_bind_controller(uint8_t player, uint8_t controller_slot);
bool local_multiplayer_bind_network_index(uint8_t player, uint8_t network_index);
const LocalPlayerSlot *local_multiplayer_slot(uint8_t player);

bool local_multiplayer_get_viewport(uint8_t player, LocalViewport *viewport);
bool local_multiplayer_get_viewport_pixels(uint8_t player, int screen_width, int screen_height,
                                           LocalViewportPixels *viewport);

void local_multiplayer_begin_player(uint8_t player);
void local_multiplayer_end_player(void);
uint8_t local_multiplayer_current_player(void);

#ifdef __cplusplus
}
#endif
