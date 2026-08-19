#include "local_multiplayer_hud.h"

#include <stdio.h>

#include "game_init.h"
#include "hud.h"
#include "local_multiplayer.h"
#include "mario.h"
#include "print.h"

static s32 clamp_health_wedges(s32 health) {
    s32 wedges = health >> 8;
    if (wedges < 0) wedges = 0;
    if (wedges > 8) wedges = 8;
    return wedges;
}

void local_multiplayer_render_hud(void) {
    if (gOverrideHideHud) return;

    const u8 count = local_multiplayer_player_count();
    if (count <= 1) return;

    for (u8 player = 0; player < count; player++) {
        if (!local_multiplayer_is_active_player(player)) continue;

        LocalViewportPixels viewport;
        if (!local_multiplayer_get_viewport_pixels(player, SCREEN_WIDTH, SCREEN_HEIGHT, &viewport)) continue;

        const struct MarioState *m = &gMarioStates[player];
        const s32 x = viewport.x + 8;
        const s32 top = SCREEN_HEIGHT - viewport.y - 14;

        char line[48];
        snprintf(line, sizeof(line), "P%d L%d H%d", player + 1, m->numLives, clamp_health_wedges(m->health));
        print_text(x, top, line);

        snprintf(line, sizeof(line), "C%d S%d", m->numCoins, m->numStars);
        print_text(x, top - 14, line);
    }
}
