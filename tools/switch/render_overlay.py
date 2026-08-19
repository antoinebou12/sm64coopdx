#!/usr/bin/env python3
"""Generate Switch-only area/render overlays with exact-match assertions."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one source match, found {count}")
    return text.replace(old, new, 1)


def overlay_area(text: str) -> str:
    text = replace_once(
        text,
        '#include "rendering_graph_node.h"\n',
        '#include "rendering_graph_node.h"\n#include "local_multiplayer_render.h"\n',
        "area include local renderer",
    )
    text = replace_once(
        text,
        "        geo_process_root(gCurrentArea->root, gViewportOverride, gViewportClip, gFBSetColor);\n",
        "        local_multiplayer_render_scene(gCurrentArea->root, gViewportOverride, gViewportClip, gFBSetColor);\n",
        "area scene render dispatch",
    )
    return text


def overlay_rendering(text: str) -> str:
    text = replace_once(
        text,
        '#include "hardcoded.h"\n',
        '#include "hardcoded.h"\n#include "local_multiplayer.h"\n',
        "render include local multiplayer",
    )

    text = replace_once(
        text,
        "                detect_and_skip_mtx_interpolation(&currList->transform, &currList->transformPrev);\n\n"
        "                struct MtxInterp *interp = growing_array_alloc(sMtxTbl, sizeof(struct MtxInterp));\n"
        "                interp->pos = gDisplayListHead;\n"
        "                interp->mtx = currList->transform;\n"
        "                interp->mtxPrev = currList->transformPrev;\n"
        "                interp->displayList = currList->displayList;\n"
        "                interp->usingCamSpace = currList->usingCamSpace;\n\n"
        "                gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transformPrev),\n"
        "                          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);\n",
        "                if (local_multiplayer_player_count() > 1) {\n"
        "                    // Each local viewport has a different camera. A single global\n"
        "                    // interpolation table cannot safely patch all passes afterward.\n"
        "                    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transform),\n"
        "                              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);\n"
        "                } else {\n"
        "                    detect_and_skip_mtx_interpolation(&currList->transform, &currList->transformPrev);\n\n"
        "                    struct MtxInterp *interp = growing_array_alloc(sMtxTbl, sizeof(struct MtxInterp));\n"
        "                    interp->pos = gDisplayListHead;\n"
        "                    interp->mtx = currList->transform;\n"
        "                    interp->mtxPrev = currList->transformPrev;\n"
        "                    interp->displayList = currList->displayList;\n"
        "                    interp->usingCamSpace = currList->usingCamSpace;\n\n"
        "                    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transformPrev),\n"
        "                              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);\n"
        "                }\n",
        "render direct current matrices for multiview",
    )

    text = replace_once(
        text,
        "#ifdef VERSION_EU\n"
        "    f32 aspect = ((f32) gCurGraphNodeRoot->width / divisor) * 1.1f;\n"
        "#else\n"
        "    f32 aspect = (f32) gCurGraphNodeRoot->width / divisor;\n"
        "#endif\n",
        "#ifdef VERSION_EU\n"
        "    f32 aspect = ((f32) gCurGraphNodeRoot->width / divisor) * 1.1f;\n"
        "#else\n"
        "    f32 aspect = (f32) gCurGraphNodeRoot->width / divisor;\n"
        "#endif\n"
        "    if (local_multiplayer_player_count() > 1) {\n"
        "        LocalViewport localViewport;\n"
        "        if (local_multiplayer_get_viewport(local_multiplayer_current_player(), &localViewport) &&\n"
        "            localViewport.height > 0.0f) {\n"
        "            aspect *= localViewport.width / localViewport.height;\n"
        "        }\n"
        "    }\n",
        "render multiview aspect ratio",
    )

    text = replace_once(
        text,
        "    guPerspective(mtx, &perspNorm, node->prevFov, aspect, near, far, 1.0f);\n\n"
        "    sPerspectiveNode = node;\n"
        "    sPerspectiveMtx = mtx;\n"
        "    sPerspectivePos = gDisplayListHead;\n"
        "    sPerspectiveAspect = aspect;\n",
        "    const bool localMultiview = local_multiplayer_player_count() > 1;\n"
        "    guPerspective(mtx, &perspNorm, localMultiview ? node->fov : node->prevFov, aspect, near, far, 1.0f);\n\n"
        "    if (!localMultiview) {\n"
        "        sPerspectiveNode = node;\n"
        "        sPerspectiveMtx = mtx;\n"
        "        sPerspectivePos = gDisplayListHead;\n"
        "        sPerspectiveAspect = aspect;\n"
        "    }\n",
        "render current perspective for multiview",
    )

    text = replace_once(
        text,
        "        if (b != NULL) {\n"
        "            clear_frame_buffer(clearColor);\n\n"
        "            sViewportClipPos = gDisplayListHead;\n"
        "            make_viewport_clip_rect(&sViewportPrev);\n\n"
        "            *viewport = *b;\n"
        "        } else if (c != NULL) {\n",
        "        if (b != NULL) {\n"
        "            const bool localMultiview = local_multiplayer_player_count() > 1;\n"
        "            if (!localMultiview || local_multiplayer_current_player() == 0) {\n"
        "                clear_frame_buffer(clearColor);\n"
        "            }\n\n"
        "            *viewport = *b;\n"
        "            if (localMultiview) {\n"
        "                make_viewport_clip_rect(viewport);\n"
        "            } else {\n"
        "                sViewportClipPos = gDisplayListHead;\n"
        "                make_viewport_clip_rect(&sViewportPrev);\n"
        "            }\n"
        "        } else if (c != NULL) {\n",
        "render multiview clear and clip",
    )

    text = replace_once(
        text,
        "        sViewport = viewport;\n"
        "        sViewportPos = gDisplayListHead;\n\n"
        "        // vvv 60 FPS PATCH vvv\n"
        "        mtxf_identity(gMatStackPrev[gMatStackIndex]);\n"
        "        gMatStackPrevFixed[gMatStackIndex] = initialMatrix;\n"
        "        // ^^^              ^^^\n\n"
        "        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(&sViewportPrev));\n",
        "        const bool localMultiview = local_multiplayer_player_count() > 1;\n"
        "        if (localMultiview) {\n"
        "            sViewport = NULL;\n"
        "            sViewportPos = NULL;\n"
        "            sViewportClipPos = NULL;\n"
        "        } else {\n"
        "            sViewport = viewport;\n"
        "            sViewportPos = gDisplayListHead;\n"
        "        }\n\n"
        "        // vvv 60 FPS PATCH vvv\n"
        "        mtxf_identity(gMatStackPrev[gMatStackIndex]);\n"
        "        gMatStackPrevFixed[gMatStackIndex] = initialMatrix;\n"
        "        // ^^^              ^^^\n\n"
        "        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(localMultiview ? viewport : &sViewportPrev));\n",
        "render direct multiview viewport",
    )

    text = replace_once(
        text,
        "        gCurGraphNodeRoot = NULL;\n"
        "    }\n"
        "}\n",
        "        gCurGraphNodeRoot = NULL;\n\n"
        "        if (local_multiplayer_player_count() > 1) {\n"
        "            // No post-pass interpolation record may survive a multi-camera pass.\n"
        "            sPerspectiveNode = NULL;\n"
        "            sPerspectiveMtx = NULL;\n"
        "            sPerspectivePos = NULL;\n"
        "            sViewport = NULL;\n"
        "            sViewportPos = NULL;\n"
        "            sViewportClipPos = NULL;\n"
        "            sBackgroundNode = NULL;\n"
        "            gBackgroundSkyboxGfx = NULL;\n"
        "            gBackgroundSkyboxMtx = NULL;\n"
        "            sCameraNode = NULL;\n"
        "            sMtxTbl->count = 0;\n"
        "            sShadowInterp->count = 0;\n"
        "        }\n"
        "    }\n"
        "}\n",
        "render clear multiview interpolation records",
    )

    return text


OVERLAYS = {
    "area": overlay_area,
    "rendering": overlay_rendering,
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
