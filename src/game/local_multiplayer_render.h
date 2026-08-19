#pragma once

#include <PR/gbi.h>

struct GraphNodeRoot;

void local_multiplayer_render_scene(struct GraphNodeRoot *root, Vp *viewport_override,
                                    Vp *viewport_clip, s32 clear_color);
