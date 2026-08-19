#!/usr/bin/env python3
"""Asset-free source invariants for the Horizon port.

This is not a replacement for a real devkitA64 full-game build or hardware test.
It catches the most expensive regressions early: desktop SDL3 leaking into the
Switch boundary, loss of the large-stack trampoline, accidental desktop input /
terminal sources in the Switch graph, missing Horizon socket/lifecycle glue,
startup paths that can crash before the first rendered frame, and local-wireless
regressions that would otherwise only appear on two physical consoles.
"""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
OVERLAYS = ROOT / "build" / "switch-ci" / "overlays"

failures: list[str] = []
passes: list[str] = []


def text(path: str | Path) -> str:
    p = path if isinstance(path, Path) else ROOT / path
    if not p.is_file():
        failures.append(f"missing required file: {p.relative_to(ROOT)}")
        return ""
    return p.read_text(encoding="utf-8")


def require(label: str, condition: bool) -> None:
    if condition:
        passes.append(label)
    else:
        failures.append(label)


makefile = text("Makefile.switch-game")
workflow = text(".github/workflows/build-switch-game.yml")
platform_h = text("src/pc/platform/switch/switch_platform.h")
platform_c = text("src/pc/platform/switch/switch_platform.c")
window_c = text("src/pc/platform/switch/switch_window_manager.c")
audio_c = text("src/pc/platform/switch/switch_audio_sdl.c")
terminal_c = text("src/pc/platform/switch/switch_terminal_stub.c")
input_c = text("src/pc/platform/switch/switch_input.c")
controller_entry = text("src/pc/controller/controller_entry_point.c")
gfx_window_h = text("src/pc/gfx/gfx_window_manager.h")
gfx_opengl_window = text("src/pc/gfx/gfx_window_opengl.c")
rom_checker = text("src/pc/rom_checker.cpp")
network_h = text("src/pc/network/network.h")
ldn_transport = text("src/pc/network/socket/socket_ldn.c")
ldn_glue = text("src/pc/network/socket/socket_ldn_glue.c")
join_panel = text("src/pc/djui/djui_panel_join.c")
ldn_browser = text("src/pc/djui/djui_panel_ldn_browser.c")

pc_main_overlay = text(OVERLAYS / "pc_main.c")
platform_overlay = text(OVERLAYS / "platform.c")
bind_overlay = text(OVERLAYS / "controller_bind_mapping.c")
controls_overlay = text(OVERLAYS / "djui_panel_controls.c")
loading_overlay = text(OVERLAYS / "loading.c")
network_overlay = text(OVERLAYS / "network.c")
host_overlay = text(OVERLAYS / "djui_panel_host.c")

# Startup crash prevention.
require(
    "8 MiB game stack constant preserved",
    "SWITCH_PLATFORM_GAME_STACK_SIZE (8u * 1024u * 1024u)" in platform_h,
)
require(
    "Horizon game entry runs through dedicated thread",
    "switch_platform_run_main_on_game_thread" in pc_main_overlay
    and "threadCreate(" in platform_c
    and "SWITCH_PLATFORM_GAME_STACK_SIZE" in platform_c,
)
require(
    "large-stack failure does not silently fall back to hbloader main thread",
    "return -1;" in platform_c and "threadStart(&thread)" in platform_c,
)
require(
    "Switch user directory is created before fs_init can reject it",
    "switch_ensure_data_root" in platform_overlay
    and 'switch_ensure_directory("sdmc:/switch")' in platform_overlay
    and "switch_ensure_directory(switch_platform_data_root())" in platform_overlay
    and "mkdir(path, 0777)" in platform_overlay
    and "errno == EEXIST" in platform_overlay,
)
require(
    "ROM directory scanning is non-throwing for absent SD paths",
    "std::error_code" in rom_checker
    and "fs::directory_iterator it(directory, ec)" in rom_checker
    and "it.increment(ec)" in rom_checker,
)
require(
    "missing-ROM screen gives actionable Switch SD path",
    "baserom.us.z64" in loading_overlay
    and "sdmc:/switch/sm64coopdx" in loading_overlay
    and "drag & drop" not in loading_overlay,
)
require(
    "missing-ROM Switch overlay is part of the build graph",
    "SWITCH_LOADING_SOURCE" in makefile
    and "source_overlay.py loading" in makefile
    and "$(BUILD_DIR)/src/pc/loading.o: $(SWITCH_LOADING_SOURCE)" in makefile,
)

# SDL boundary and renderer safety.
require(
    "window manager public API selects SDL2 on Switch",
    "#ifdef __SWITCH__\n#include <SDL2/SDL.h>\n#else\n#include <SDL3/SDL.h>" in gfx_window_h,
)
require(
    "OpenGL window backend has explicit Horizon branch",
    "#ifdef __SWITCH__\n#include <SDL2/SDL.h>" in gfx_opengl_window,
)
require(
    "Horizon disables pre-init MSAA context probing",
    "configWindow.msaa = 0;" in gfx_opengl_window,
)
require(
    "Horizon destroys SDL2 GL contexts with SDL_GL_DeleteContext",
    "SDL_GL_DeleteContext(ctx);" in gfx_opengl_window,
)
require(
    "Horizon fails cleanly when SDL2/GLES startup fails",
    "Switch SDL2 window creation failed" in gfx_opengl_window
    and "Switch GLES2 context creation failed" in gfx_opengl_window
    and "Switch GLES2 context activation failed" in gfx_opengl_window,
)
require(
    "controller binding overlay is SDL2-only",
    "#include <SDL2/SDL.h>" in bind_overlay
    and "SDL_NUM_SCANCODES" in bind_overlay
    and "SDL_SCANCODE_COUNT" not in bind_overlay,
)

# Build graph isolation.
try:
    exclusion_block = makefile.split("SWITCH_EXCLUDE_C :=", 1)[1].split("SWITCH_EXCLUDE_CPP :=", 1)[0]
except IndexError:
    exclusion_block = ""

for desktop_source in (
    "src/pc/audio/audio_sdl.c",
    "src/pc/controller/controller_sdl.c",
    "src/pc/controller/controller_mouse.c",
    "src/pc/gfx/gfx_window_manager.c",
    "src/pc/terminal.c",
):
    require(
        f"Switch graph excludes desktop source {desktop_source}",
        desktop_source in exclusion_block,
    )
require("Switch linker avoids host -pthread", "-pthread" not in makefile)
require("Switch linker includes libnx", "-lnx" in makefile)
require("Switch build forces SDL2 portlibs", "-DHAVE_SDL2=1" in makefile)
require("Switch build forces GLES", "-DUSE_GLES=1" in makefile)

# Horizon lifecycle and networking.
require(
    "Horizon applet loop is pumped from real window loop",
    "switch_platform_main_loop(&sPlatformState)" in window_c,
)
require(
    "Horizon HOME/exit callback is wired to game_exit",
    "switch_platform_set_exit_callback(game_exit)" in window_c,
)
require(
    "libnx socket service initializes before CoopDX networking",
    "socketInitializeDefault()" in platform_c and "socketExit()" in platform_c,
)
require(
    "applet-memory launch emits a title-override warning",
    "running in Horizon applet mode" in platform_c and "title override/full application mode" in platform_c,
)
require(
    "Switch data root stays self-contained on SD card",
    'return "sdmc:/switch/sm64coopdx";' in platform_c
    and "switch_platform_data_root()" in platform_overlay,
)

# Local wireless: keep libnx LDN as the control plane and BSD UDP as the data
# plane. In particular, do not regress to action-frame packet transport: it has
# a much smaller payload ceiling and previously caused LDN sysmodule wedges.
require(
    "LDN backend is a first-class Switch network system",
    "NS_LDN" in network_h
    and "case NS_LDN:" in network_overlay
    and "&gNetworkSystemLdn" in network_overlay,
)
require(
    "LDN reconnect restores the LDN backend instead of falling back to Socket",
    "gNetworkSystem == &gNetworkSystemLdn" in network_overlay
    and "network_set_system(NS_LDN)" in network_overlay,
)
require(
    "LDN gameplay transport uses non-blocking UDP",
    "SOCK_DGRAM" in ldn_transport
    and "O_NONBLOCK" in ldn_transport
    and "recvfrom(" in ldn_transport
    and "sendto(" in ldn_transport,
)
require(
    "LDN gameplay transport never uses action-frame IPC",
    "ldnSendActionFrame" not in ldn_transport
    and "ldnScanActionFrame" not in ldn_transport,
)
require(
    "platform exclusively owns the libnx BSD socket service lifetime",
    "socketInitializeDefault()" in platform_c
    and "socketExit()" in platform_c
    and "socketInitializeDefault()" not in ldn_transport
    and "socketExit()" not in ldn_transport,
)
require(
    "LDN peer identity is bound by IPv4 address",
    "ldn_save_id_impl" in ldn_transport
    and "ldn_match_addr_impl" in ldn_transport
    and "sLdnAddr[localIndex] = sLdnAddr[0]" in ldn_transport,
)
require(
    "LDN join retries transient association failures and rescans",
    "attempt < 10" in ldn_transport
    and "svcSleepThread(400000000ULL)" in ldn_transport
    and "ldnScan(" in ldn_transport,
)
require(
    "LDN names are sanitized before entering fixed libnx user_name fields",
    "ldn_fill_user_name" in ldn_transport
    and "c >= 0x20 && c <= 0x7E" in ldn_transport,
)
require(
    "LDN backend preserves physical association across CoopDX reconnects",
    "ldn_prepare_reconnect_impl" in ldn_transport
    and "if (reconnecting)" in ldn_glue,
)
require(
    "Switch Join menu preserves both Local Wireless and Direct IP",
    '"LOCAL WIRELESS"' in join_panel
    and "djui_panel_join_direct_create" in join_panel
    and "djui_panel_ldn_browser_create" in join_panel,
)
require(
    "local-wireless host uses the standard CoopDX host lifecycle",
    "configNetworkSystem = NS_LDN" in ldn_browser
    and "djui_panel_do_host(false, true)" in ldn_browser,
)
require(
    "fresh normal Host flow cannot inherit a stale LDN selection",
    "configNetworkSystem == NS_LDN" in host_overlay
    and "configNetworkSystem = NS_SOCKET" in host_overlay,
)
require(
    "authoritative Switch CI cross-compiles real LDN integration objects",
    "Compile real local-wireless integration" in workflow
    and "socket_ldn.o" in workflow
    and "socket_ldn_glue.o" in workflow
    and "djui_panel_ldn_browser.o" in workflow
    and "build/us_switch/src/pc/network/network.o" in workflow,
)

# Input/audio console-specific behavior.
require(
    "native libnx controller backend is selected on Switch",
    '#ifdef __SWITCH__\n#include "controller_switch.h"' in controller_entry
    and "&controller_switch" in controller_entry,
)
require(
    "native input supports multiple Horizon pad slots",
    "SWITCH_INPUT_MAX_PLAYERS" in input_c,
)
require(
    "Switch audio queue is explicitly bounded",
    "SDL_GetQueuedAudioSize" in audio_c and "< 24000" in audio_c,
)
require(
    "desktop TTY/linenoise is replaced on Horizon",
    "void terminal_init(void) {}" in terminal_c
    and "linenoise" not in terminal_c,
)
require(
    "controls UI no longer enumerates SDL gamepads on Horizon",
    '"Nintendo Switch"' in controls_overlay and "SDL_GetJoysticks" not in controls_overlay,
)

print("Switch CI source invariants:")
for item in passes:
    print(f"  PASS: {item}")
for item in failures:
    print(f"  FAIL: {item}", file=sys.stderr)

if failures:
    print(f"\n{len(failures)} Switch invariant(s) failed.", file=sys.stderr)
    raise SystemExit(1)

print(f"\nAll {len(passes)} Switch invariants passed.")
