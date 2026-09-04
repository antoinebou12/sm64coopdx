#!/usr/bin/env python3
"""Static regression checks for New 3DS hardware-only stability fixes."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def define_int(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(\d+)\s*(?:/\*.*)?$", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing integer define: {name}")
    return int(match.group(1))


def make_var(text: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}\s*\?=\s*([^\s#]+)", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing make variable: {name}")
    return match.group(1)


def is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def main() -> int:
    gfx_h = read("src/pc/gfx/gfx.h")
    renderer = read("src/pc/gfx/gfx_citro3d_new3ds.c")
    guard = read("src/pc/platform/new3ds/new3ds_gfx_state_guard.c")
    exception_handler = read("src/pc/platform/new3ds/new3ds_exception_handler.c")
    makefile = read("Makefile.new3ds-game")
    workflow = read(".github/workflows/build-new3ds.yml")

    cache_match = re.search(
        r"#if defined\(__3DS__\)\s*\n#define MAX_CACHED_TEXTURES\s+(\d+)",
        gfx_h,
        re.MULTILINE,
    )
    require(cache_match is not None, "New 3DS MAX_CACHED_TEXTURES override is missing")
    cache_size = int(cache_match.group(1))
    renderer_pool = define_int(renderer, "NEW3DS_TEXTURE_POOL_SIZE")
    require(is_power_of_two(cache_size), f"New 3DS texture cache must be power-of-two, got {cache_size}")
    require(
        cache_size < renderer_pool,
        f"texture cache must leave backend headroom: cache={cache_size}, backend={renderer_pool}",
    )
    require(cache_size == 512, f"unexpected New 3DS texture cache budget: {cache_size}")
    require(renderer_pool == 768, f"unexpected Citro3D texture pool budget: {renderer_pool}")

    # Match mkst/sm64-port: T = 1 - (t * scale); S straight. No depth-off S mirror.
    require(
        "1.0f - (src[program->tex_offset[0] + 1] * new3ds_texture_scale_t(0))" in renderer,
        "renderer must use mkst-style T = 1 - (t * scale)",
    )
    require(
        "(sDepthTest ? s0 : (1.0f - s0))" not in renderer,
        "renderer must not invert S for depth-off draws",
    )
    require(
        "src[program->tex_offset[0]] * new3ds_texture_scale_s(0)" in renderer,
        "renderer does not map S straight through",
    )
    require(
        "new3ds_wipe_texture_pool" in renderer,
        "texture pool must wipe on overflow instead of returning id 0",
    )
    require(
        "clamped_x" in renderer and "clamped_y" in renderer,
        "swizzle must edge-clamp NPOT padding (not modulo-wrap)",
    )
    require(
        "sDepthWriteApplied = !sDepthWrite;" in renderer,
        "fog pass does not invalidate depth-write cache before restore",
    )

    # The backend T invert already lands on source row v * height, so the DJUI
    # font path must feed atlas V straight through (only the desktop UV nudge is
    # dropped). Pre-inverting V here double-flips and garbles menu/title text.
    djui = read("src/pc/djui/djui_gfx.c")
    require(
        "(h - tileY - tileH)" not in djui,
        "DJUI 3DS font path must not pre-invert atlas V (double-flips glyphs)",
    )
    require(
        "const f32 offsetX = 0.0f;" in djui and "const f32 offsetY = 0.0f;" in djui,
        "DJUI 3DS font path must drop the desktop UV nudge",
    )

    # DJUI runs at a fixed 0.5x device scale on the 400x240 top screen, so a
    # glyph quad of N units covers N/2 pixels. Only whole texel:pixel ratios
    # decimate evenly under GPU_NEAREST; arbitrary shrink factors drop strokes
    # and smear the text. Body text therefore stays on the 8x16 normal atlas at
    # defaultFontScale (exactly 1:1).
    text_src = read("src/pc/djui/djui_text.c")
    require(
        "gDjuiFonts[FONT_ALIASED]" not in text_src,
        "3DS must not force the 16x32 aliased atlas (never 1:1 at 0.5x scale)",
    )
    button = read("src/pc/djui/djui_button.c")
    require(
        "defaultFontScale * 0.55f" not in button,
        "3DS button text must render at defaultFontScale (1:1), not shrunk",
    )
    require(
        "const f32 buttonHeight = 44.0f;" in button,
        "3DS buttons must be tall enough for the 32-unit body glyph cell",
    )
    for path in (
        "src/pc/djui/djui_panel_host.c",
        "src/pc/djui/djui_panel_coopnet.c",
        "src/pc/djui/djui_panel_host_settings.c",
        "src/pc/djui/djui_panel_join_direct.c",
        "src/pc/djui/djui_panel_join_lobbies.c",
    ):
        panel = read(path)
        require(
            "font->defaultFontScale *" not in panel,
            f"{path} shrinks 3DS body text below the 1:1 atlas fit",
        )

    require(
        "gfx_citro3d_new3ds_api.draw_triangles = new3ds_gfx_guard_draw_triangles;" in guard,
        "PICA draw-state guard is not installed",
    )
    require("C3D_DepthTest(" in guard, "depth state is not reasserted after backend draws")
    require("C3D_DepthMap(" in guard, "depth mapping is not reasserted after backend draws")
    require("C3D_AlphaBlend(" in guard, "blend state is not reasserted after backend draws")

    # The optional handler may still be used for targeted SD-log experiments,
    # but normal hardware builds must let Luma/system exception handling see the
    # original fault rather than swallowing it behind a permanent black screen.
    require("svcBreak(USERBREAK_PANIC);" in exception_handler, "optional handler does not re-raise fatal faults")
    require(
        "while (aptMainLoop())" not in exception_handler,
        "fatal exception handler still contains the black-screen wait loop",
    )
    require(
        make_var(makefile, "NEW3DS_USER_EXCEPTIONS") == "0",
        "custom libctru exception handler must be disabled by default on hardware",
    )

    require(make_var(makefile, "NEW3DS_APP_VERSION") == "0.0.1", "3DSX/CIA semantic version is not 0.0.1")
    require(make_var(makefile, "NEW3DS_CIA_VERSION") == "1", "CIA numeric title version is not 1")
    require("-ver $(NEW3DS_CIA_VERSION)" in makefile, "makerom is not receiving the CIA title version")
    require(
        "new3ds_gfx_state_guard.o" in makefile,
        "hardware state guard is missing from the integration compile gate",
    )

    require("new3ds-3dsx new3ds-cia" in workflow, "CI does not build both 3DSX and CIA")
    require("sm64coopdx.cia" in workflow, "CI does not verify/upload the CIA artifact")
    require(
        "python3 tools/new3ds/tests/test_hardware_stability.py" in workflow,
        "CI does not execute the hardware stability regression test",
    )

    print(
        "New 3DS hardware stability checks passed: "
        f"texture_cache={cache_size}, renderer_pool={renderer_pool}, "
        "version=0.0.1, user_exceptions=off"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"hardware stability regression: {exc}", file=sys.stderr)
        raise SystemExit(1)
