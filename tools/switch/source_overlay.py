#!/usr/bin/env python3
"""Generate narrow Switch-only source overlays from current upstream files.

Each transformation is intentionally exact. If upstream changes the expected
code, the Switch build fails here instead of silently compiling an outdated
fork of a large source file.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_exact_count(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


def overlay_pc_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "pc/mods/mods.h"\n',
        '#include "pc/mods/mods.h"\n#ifdef __SWITCH__\n#include "pc/platform/switch/switch_platform.h"\n#endif\n',
        "pc_main switch platform include",
    )

    text = replace_once(
        text,
        'int main(int argc, char *argv[]) {\n',
        'static int sm64_main(int argc, char *argv[]);\n\n'
        'static int switch_game_main(int argc, char **argv) {\n'
        '    return sm64_main(argc, argv);\n'
        '}\n\n'
        'int main(int argc, char *argv[]) {\n'
        '    return switch_platform_run_main_on_game_thread(switch_game_main, argc, argv);\n'
        '}\n\n'
        'static int sm64_main(int argc, char *argv[]) {\n',
        "pc_main big stack entry",
    )

    text = replace_once(
        text,
        '    if (gCLIOpts.network != NT_SERVER && !gCLIOpts.skipUpdateCheck) {\n'
        '        check_for_updates();\n'
        '    }\n',
        '    /* The NRO is updated by replacing the file on the SD card. */\n'
        '#ifndef __SWITCH__\n'
        '    if (gCLIOpts.network != NT_SERVER && !gCLIOpts.skipUpdateCheck) {\n'
        '        check_for_updates();\n'
        '    }\n'
        '#endif\n',
        "pc_main disable Switch update check",
    )
    return text


def overlay_platform(text: str) -> str:
    text = replace_once(
        text,
        '#elif defined(__APPLE__)\n#include <mach-o/dyld.h>\n#else\n#include <unistd.h>\n#endif\n',
        '#elif defined(__APPLE__)\n#include <mach-o/dyld.h>\n#elif !defined(__SWITCH__)\n#include <unistd.h>\n#endif\n',
        "platform unistd guard",
    )

    marker = '#else\n\n#include <SDL3/SDL.h>\n\nconst char *sys_user_path(void) {'
    switch_impl = '''#elif defined(__SWITCH__)\n\n#include <errno.h>\n#include <sys/stat.h>\n#include "pc/platform/switch/switch_platform.h"\n\nstatic bool switch_ensure_data_root(void) {\n    const char *root = switch_platform_data_root();\n    if (mkdir(root, 0777) == 0 || errno == EEXIST) {\n        return true;\n    }\n    return false;\n}\n\nconst char *sys_user_path(void) {\n    return switch_ensure_data_root() ? switch_platform_data_root() : NULL;\n}\n\nconst char *sys_resource_path(void) {\n    return switch_platform_data_root();\n}\n\nconst char *sys_exe_path_dir(void) {\n    return switch_platform_data_root();\n}\n\nconst char *sys_exe_path_file(void) {\n    return "sdmc:/switch/sm64coopdx/sm64coopdx.nro";\n}\n\nstatic void sys_fatal_impl(const char *msg) {\n    fprintf(stderr, "FATAL ERROR:\\n%s\\n", msg);\n    fflush(stderr);\n    exit(1);\n}\n\n#else\n\n#include <SDL3/SDL.h>\n\nconst char *sys_user_path(void) {'''
    text = replace_once(text, marker, switch_impl, "platform Switch filesystem branch")
    return text


def overlay_controller_bind(text: str) -> str:
    text = replace_once(
        text,
        '#include <SDL3/SDL.h>\n',
        '#include <SDL2/SDL.h>\n',
        "controller bind SDL2 include",
    )
    # SDL3 renamed SDL2's fixed 512-entry scancode count symbol.
    text = replace_exact_count(
        text,
        'SDL_SCANCODE_COUNT',
        'SDL_NUM_SCANCODES',
        6,
        "controller bind SDL2 scancode count",
    )
    text = replace_once(
        text,
        'SDL_Keycode kc = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);',
        'SDL_Keycode kc = SDL_GetKeyFromScancode(sc);',
        "controller bind SDL2 key lookup",
    )
    return text


def overlay_djui_controls(text: str) -> str:
    start = text.index('        int numJoys;\n')
    end_marker = '        free(gamepadChoices);\n'
    end = text.index(end_marker, start) + len(end_marker)
    replacement = '''        /* Native Horizon input is not an SDL gamepad. */\n        char *gamepadChoices[] = { "Nintendo Switch" };\n        int numJoys = 1;\n        configGamepadNumber = 0;\n        djui_selectionbox_create(body, DLANG(CONTROLS, GAMEPAD), gamepadChoices, numJoys, &configGamepadNumber, NULL);\n'''
    return text[:start] + replacement + text[end:]


def overlay_loading(text: str) -> str:
    return replace_once(
        text,
        '    loading_screen_set_segment_text("No rom detected, drag & drop Super Mario 64 (U) [!].z64 on to this screen");',
        '    loading_screen_set_segment_text("No ROM detected. Copy baserom.us.z64 to sdmc:/switch/sm64coopdx and restart the app.");',
        "Switch missing-ROM instructions",
    )


OVERLAYS = {
    "pc_main": overlay_pc_main,
    "platform": overlay_platform,
    "controller_bind": overlay_controller_bind,
    "djui_controls": overlay_djui_controls,
    "loading": overlay_loading,
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=OVERLAYS)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    output = OVERLAYS[args.mode](text)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
