#include <assert.h>
#include <stdio.h>

#include "game/local_multiplayer.h"

static void expect_pixels(unsigned player, int x, int y, int w, int h) {
    LocalViewportPixels vp = { 0 };
    assert(local_multiplayer_get_viewport_pixels((uint8_t)player, 1280, 720, &vp));
    assert(vp.x == x);
    assert(vp.y == y);
    assert(vp.width == w);
    assert(vp.height == h);
}

int main(void) {
    local_multiplayer_reset();
    assert(local_multiplayer_player_count() == 1);
    assert(local_multiplayer_is_active_player(0));
    assert(!local_multiplayer_is_active_player(1));
    expect_pixels(0, 0, 0, 1280, 720);

    assert(local_multiplayer_sync_controller_count(0, true) == 1);
    assert(local_multiplayer_sync_controller_count(4, false) == 1);

    assert(local_multiplayer_sync_controller_count(2, true) == 2);
    assert(local_multiplayer_layout() == LOCAL_SPLIT_TWO_HORIZONTAL);
    expect_pixels(0, 0, 0, 1280, 360);
    expect_pixels(1, 0, 360, 1280, 360);

    local_multiplayer_set_layout(LOCAL_SPLIT_TWO_VERTICAL);
    assert(local_multiplayer_layout() == LOCAL_SPLIT_TWO_VERTICAL);
    expect_pixels(0, 0, 0, 640, 720);
    expect_pixels(1, 640, 0, 640, 720);

    assert(local_multiplayer_sync_controller_count(3, true) == 3);
    assert(local_multiplayer_layout() == LOCAL_SPLIT_THREE);
    expect_pixels(0, 0, 0, 640, 360);
    expect_pixels(1, 640, 0, 640, 360);
    expect_pixels(2, 0, 360, 1280, 360);

    assert(local_multiplayer_bind_controller(2, 3));
    assert(local_multiplayer_slot(2)->controller_slot == 3);
    assert(local_multiplayer_bind_network_index(2, 7));
    assert(local_multiplayer_slot(2)->network_index == 7);
    local_multiplayer_reset_network_bindings();
    assert(local_multiplayer_slot(2)->network_index == LOCAL_MULTIPLAYER_INVALID_NETWORK_INDEX);

    assert(local_multiplayer_sync_controller_count(4, true) == 4);
    assert(local_multiplayer_layout() == LOCAL_SPLIT_FOUR);
    expect_pixels(0, 0, 0, 640, 360);
    expect_pixels(1, 640, 0, 640, 360);
    expect_pixels(2, 0, 360, 640, 360);
    expect_pixels(3, 640, 360, 640, 360);

    local_multiplayer_begin_player(3);
    assert(local_multiplayer_current_player() == 3);
    local_multiplayer_end_player();
    assert(local_multiplayer_current_player() == 0);

    assert(!local_multiplayer_set_player_count(0));
    assert(!local_multiplayer_set_player_count(5));

    puts("local multiplayer tests passed");
    return 0;
}
