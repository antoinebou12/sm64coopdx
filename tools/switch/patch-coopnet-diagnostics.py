#!/usr/bin/env python3
"""Inject Horizon-only diagnostics into the pinned CoopNet source tree.

Every edit uses an exact anchor and must match exactly once. If the pinned
upstream source changes, the dependency build fails loudly.
"""

from pathlib import Path
import sys


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"{path}: expected one diagnostics anchor, found {count}\nANCHOR:\n{old}"
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def after(path: Path, anchor: str, addition: str) -> None:
    replace_once(path, anchor, anchor + addition)


def before(path: Path, anchor: str, addition: str) -> None:
    replace_once(path, anchor, addition + anchor)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch-coopnet-diagnostics.py <coopnet-source-dir>")

    root = Path(sys.argv[1])
    common = root / "common"
    client = common / "client.cpp"
    peer = common / "peer.cpp"

    for path in (client, peer):
        if not path.is_file():
            raise SystemExit(f"missing pinned CoopNet source: {path}")

    (common / "horizon_diag.hpp").write_text(
        '''#pragma once

#include <cstdint>

#ifdef __SWITCH__
extern "C" void coopnet_horizon_diag(const char* event,
                                     uint64_t value0,
                                     uint64_t value1,
                                     const char* text) __attribute__((weak));

static inline void horizon_diag(const char* event, uint64_t value0 = 0,
                                uint64_t value1 = 0, const char* text = nullptr) {
    if (coopnet_horizon_diag) {
        coopnet_horizon_diag(event, value0, value1, text);
    }
}
#else
static inline void horizon_diag(const char*, uint64_t = 0,
                                uint64_t = 0, const char* = nullptr) {}
#endif
''',
        encoding="utf-8",
    )

    # Central TCP signaling diagnostics. Never pass player names or passwords.
    after(client, '#include "logging.hpp"\n', '#include "horizon_diag.hpp"\n')
    before(
        client,
        '    mConnection->mSocket = SocketInitialize(AF_INET, SOCK_STREAM, IPPROTO_TCP);\n',
        '    horizon_diag("signaling.socket_create", 0, 0, aHost.c_str());\n',
    )
    after(
        client,
        '        LOG_ERROR("Socket failed");\n',
        '        horizon_diag("signaling.socket_error", (uint64_t)errno, 0, aHost.c_str());\n',
    )
    before(
        client,
        '    mConnection->mAddress.sin_addr.s_addr = GetAddrFromDomain(aHost);\n',
        '    horizon_diag("signaling.dns_begin", 0, 0, aHost.c_str());\n',
    )
    after(
        client,
        '    mConnection->mAddress.sin_addr.s_addr = GetAddrFromDomain(aHost);\n',
        '    horizon_diag("signaling.dns_result", (uint64_t)mConnection->mAddress.sin_addr.s_addr, 0, aHost.c_str());\n',
    )
    before(
        client,
        '    int rc = connect(mConnection->mSocket, (struct sockaddr*) &mConnection->mAddress, sizeof(struct sockaddr_in));\n',
        '    horizon_diag("signaling.connect_begin", (uint64_t)aPort, 0, aHost.c_str());\n',
    )
    after(
        client,
        '                LOG_ERROR("Connection timed out");\n',
        '                horizon_diag("signaling.connect_error", (uint64_t)ETIMEDOUT, 0, aHost.c_str());\n',
    )
    after(
        client,
        '                LOG_ERROR("Error while waiting for connection");\n',
        '                horizon_diag("signaling.connect_error", (uint64_t)errno, 0, aHost.c_str());\n',
    )
    after(
        client,
        '            LOG_ERROR("Connect failed: %u", rc);\n',
        '            horizon_diag("signaling.connect_error", (uint64_t)rc, 0, aHost.c_str());\n',
    )
    before(
        client,
        '    mConnection->Begin(nullptr);\n',
        '    horizon_diag("signaling.connect_ok", (uint64_t)aPort, 0, aHost.c_str());\n',
    )

    # ICE/libjuice diagnostics. TURN credentials are deliberately never passed.
    after(peer, '#include <cstring>\n', '#include <cerrno>\n')
    after(peer, '#include "utils.hpp"\n', '#include "horizon_diag.hpp"\n')
    after(
        peer,
        '    LOG_INFO("STUN server: %s, %u", config.stun_server_host, config.stun_server_port);\n',
        '    horizon_diag("ice.stun_server", mId, config.stun_server_port, config.stun_server_host);\n',
    )
    before(peer, '    mAgent = juice_create(&config);\n', '    horizon_diag("ice.agent_create_before", mId);\n')
    after(
        peer,
        '    mAgent = juice_create(&config);\n',
        '    horizon_diag("ice.agent_create_after", mId, (uint64_t)(uintptr_t)mAgent);\n',
    )
    before(
        peer,
        '    juice_set_remote_description(mAgent, aSdp);\n',
        '    horizon_diag("ice.remote_description_before", mId);\n',
    )
    after(
        peer,
        '    juice_set_remote_description(mAgent, aSdp);\n',
        '    horizon_diag("ice.remote_description_after", mId);\n',
    )

    # There are two legitimate gather calls: controller Connect and non-controller CandidateDone.
    text = peer.read_text(encoding="utf-8")
    gather = '        juice_gather_candidates(mAgent);\n'
    if text.count(gather) != 2:
        raise SystemExit(f"{peer}: expected two juice_gather_candidates anchors, found {text.count(gather)}")
    peer.write_text(
        text.replace(
            gather,
            '        horizon_diag("ice.gather_before", mId);\n'
            '        juice_gather_candidates(mAgent);\n'
            '        horizon_diag("ice.gather_after", mId);\n',
        ),
        encoding="utf-8",
    )

    replace_once(
        peer,
        '    juice_send(mAgent, (const char*)aData, aDataLength);\n',
        '    errno = 0;\n'
        '    int rc = juice_send(mAgent, (const char*)aData, aDataLength);\n'
        '    if (rc != 0) {\n'
        '        horizon_diag("ice.send_error", mId, (uint64_t)errno);\n'
        '    }\n',
    )
    before(peer, '        juice_destroy(mAgent);\n', '        horizon_diag("ice.agent_destroy_before", mId);\n')
    after(peer, '        juice_destroy(mAgent);\n', '        horizon_diag("ice.agent_destroy_after", mId);\n')
    before(
        peer,
        '    juice_add_remote_candidate(mAgent, aSdp);\n',
        '    horizon_diag("ice.remote_candidate_before", mId, 0, aSdp);\n',
    )
    after(
        peer,
        '    juice_add_remote_candidate(mAgent, aSdp);\n',
        '    horizon_diag("ice.remote_candidate_after", mId);\n',
    )
    after(
        peer,
        '    LOG_INFO("State change (%" PRIu64 "): %s", mId, juice_state_to_string(aState));\n',
        '    horizon_diag("ice.state", mId, (uint64_t)aState, juice_state_to_string(aState));\n',
    )
    after(
        peer,
        '            LOG_INFO("Remote candidate: %s\\n", remote);\n',
        '            horizon_diag("ice.selected_local", mId, 0, local);\n'
        '            horizon_diag("ice.selected_remote", mId, 0, remote);\n',
    )
    after(
        peer,
        '                LOG_INFO("Using TURN relay server");\n',
        '                horizon_diag("ice.turn_selected", mId);\n',
    )
    after(
        peer,
        '    LOG_INFO("Candidate (%" PRIu64 "): %s", mId, aSdp);\n',
        '    horizon_diag("ice.local_candidate", mId, 0, aSdp);\n',
    )
    after(
        peer,
        '    LOG_INFO("Gathering done (%" PRIu64 ")", mId);\n',
        '    horizon_diag("ice.gathering_done", mId);\n',
    )

    print(f"Injected Horizon diagnostics into {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
