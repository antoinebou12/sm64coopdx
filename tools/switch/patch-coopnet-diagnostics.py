#!/usr/bin/env python3
"""Inject Horizon-only diagnostics into the pinned CoopNet source tree.

This intentionally uses exact-text replacements. If the pinned upstream source
changes, the Switch dependency build fails loudly instead of silently applying
instrumentation to the wrong code path.
"""

from pathlib import Path
import sys


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"{path}: expected exactly one diagnostics anchor, found {count}\n"
            f"ANCHOR:\n{old}"
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


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

    replace_once(
        client,
        '#include "logging.hpp"\n',
        '#include "logging.hpp"\n#include "horizon_diag.hpp"\n',
    )

    replace_once(
        client,
        '''    // setup a socket
    mConnection->mSocket = SocketInitialize(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(mConnection->mSocket <= 0)
    {
        LOG_ERROR("Socket failed");
        return false;
    }

    // type of socket created
    mConnection->mAddress.sin_family = AF_INET;
    mConnection->mAddress.sin_addr.s_addr = GetAddrFromDomain(aHost);
    mConnection->mAddress.sin_port = htons(aPort);

    SocketSetOptions(mConnection->mSocket);
    errno = 0;

    int rc = connect(mConnection->mSocket, (struct sockaddr*) &mConnection->mAddress, sizeof(struct sockaddr_in));
''',
        '''    // setup a socket
    horizon_diag("signaling.socket_create", 0, 0, aHost.c_str());
    mConnection->mSocket = SocketInitialize(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(mConnection->mSocket <= 0)
    {
        LOG_ERROR("Socket failed");
        horizon_diag("signaling.socket_error", (uint64_t)errno, 0, aHost.c_str());
        return false;
    }

    // type of socket created
    mConnection->mAddress.sin_family = AF_INET;
    horizon_diag("signaling.dns_begin", 0, 0, aHost.c_str());
    mConnection->mAddress.sin_addr.s_addr = GetAddrFromDomain(aHost);
    horizon_diag("signaling.dns_result", (uint64_t)mConnection->mAddress.sin_addr.s_addr, 0, aHost.c_str());
    mConnection->mAddress.sin_port = htons(aPort);

    SocketSetOptions(mConnection->mSocket);
    errno = 0;

    horizon_diag("signaling.connect_begin", (uint64_t)aPort, 0, aHost.c_str());
    int rc = connect(mConnection->mSocket, (struct sockaddr*) &mConnection->mAddress, sizeof(struct sockaddr_in));
''',
    )

    replace_once(
        client,
        '''            if (selectResult == 0) {
                LOG_ERROR("Connection timed out");
                SocketClose(mConnection->mSocket);
                return false;
            } else if (selectResult < 0) {
                LOG_ERROR("Error while waiting for connection");
                SocketClose(mConnection->mSocket);
                return false;
            }
        } else {
            // Other error occurred during connect
            LOG_ERROR("Connect failed: %u", rc);
            SocketClose(mConnection->mSocket);
            return false;
        }
''',
        '''            if (selectResult == 0) {
                LOG_ERROR("Connection timed out");
                horizon_diag("signaling.connect_error", (uint64_t)ETIMEDOUT, 0, aHost.c_str());
                SocketClose(mConnection->mSocket);
                return false;
            } else if (selectResult < 0) {
                LOG_ERROR("Error while waiting for connection");
                horizon_diag("signaling.connect_error", (uint64_t)errno, 0, aHost.c_str());
                SocketClose(mConnection->mSocket);
                return false;
            }
        } else {
            // Other error occurred during connect
            LOG_ERROR("Connect failed: %u", rc);
            horizon_diag("signaling.connect_error", (uint64_t)rc, 0, aHost.c_str());
            SocketClose(mConnection->mSocket);
            return false;
        }
''',
    )

    replace_once(
        client,
        '    mConnection->Begin(nullptr);\n\n    MPacketInfo',
        '    horizon_diag("signaling.connect_ok", (uint64_t)aPort, 0, aHost.c_str());\n'
        '    mConnection->Begin(nullptr);\n\n    MPacketInfo',
    )

    replace_once(peer, '#include <cstring>\n', '#include <cstring>\n#include <cerrno>\n')
    replace_once(peer, '#include "utils.hpp"\n', '#include "utils.hpp"\n#include "horizon_diag.hpp"\n')

    replace_once(
        peer,
        '    LOG_INFO("STUN server: %s, %u", config.stun_server_host, config.stun_server_port);\n',
        '    LOG_INFO("STUN server: %s, %u", config.stun_server_host, config.stun_server_port);\n'
        '    horizon_diag("ice.stun_server", mId, config.stun_server_port, config.stun_server_host);\n',
    )

    replace_once(
        peer,
        '    mConnected = false;\n    mAgent = juice_create(&config);\n',
        '    mConnected = false;\n'
        '    horizon_diag("ice.agent_create_before", mId);\n'
        '    mAgent = juice_create(&config);\n'
        '    horizon_diag("ice.agent_create_after", mId, (uint64_t)(uintptr_t)mAgent);\n',
    )

    replace_once(
        peer,
        '''    juice_set_remote_description(mAgent, aSdp);
    if (mControlling) {
        juice_gather_candidates(mAgent);
    }
''',
        '''    horizon_diag("ice.remote_description_before", mId);
    juice_set_remote_description(mAgent, aSdp);
    horizon_diag("ice.remote_description_after", mId);
    if (mControlling) {
        horizon_diag("ice.gather_before", mId);
        juice_gather_candidates(mAgent);
        horizon_diag("ice.gather_after", mId);
    }
''',
    )

    replace_once(
        peer,
        '''    juice_send(mAgent, (const char*)aData, aDataLength);
    return true;
''',
        '''    errno = 0;
    int rc = juice_send(mAgent, (const char*)aData, aDataLength);
    if (rc != 0) {
        horizon_diag("ice.send_error", mId, (uint64_t)errno);
    }
    return true;
''',
    )

    replace_once(
        peer,
        '        juice_destroy(mAgent);\n        mAgent = nullptr;\n',
        '        horizon_diag("ice.agent_destroy_before", mId);\n'
        '        juice_destroy(mAgent);\n'
        '        horizon_diag("ice.agent_destroy_after", mId);\n'
        '        mAgent = nullptr;\n',
    )

    replace_once(
        peer,
        '    juice_add_remote_candidate(mAgent, aSdp);\n',
        '    horizon_diag("ice.remote_candidate_before", mId, 0, aSdp);\n'
        '    juice_add_remote_candidate(mAgent, aSdp);\n'
        '    horizon_diag("ice.remote_candidate_after", mId);\n',
    )

    replace_once(
        peer,
        '''    if (!mControlling) {
        juice_gather_candidates(mAgent);
    }
''',
        '''    if (!mControlling) {
        horizon_diag("ice.gather_before", mId);
        juice_gather_candidates(mAgent);
        horizon_diag("ice.gather_after", mId);
    }
''',
    )

    replace_once(
        peer,
        '    LOG_INFO("State change (%" PRIu64 "): %s", mId, juice_state_to_string(aState));\n',
        '    LOG_INFO("State change (%" PRIu64 "): %s", mId, juice_state_to_string(aState));\n'
        '    horizon_diag("ice.state", mId, (uint64_t)aState, juice_state_to_string(aState));\n',
    )

    replace_once(
        peer,
        r'''            LOG_INFO("Local candidate : %s\n", local);
            LOG_INFO("Remote candidate: %s\n", remote);
''',
        r'''            LOG_INFO("Local candidate : %s\n", local);
            LOG_INFO("Remote candidate: %s\n", remote);
            horizon_diag("ice.selected_local", mId, 0, local);
            horizon_diag("ice.selected_remote", mId, 0, remote);
''',
    )

    replace_once(
        peer,
        '                LOG_INFO("Using TURN relay server");\n',
        '                LOG_INFO("Using TURN relay server");\n'
        '                horizon_diag("ice.turn_selected", mId);\n',
    )

    replace_once(
        peer,
        '    LOG_INFO("Candidate (%" PRIu64 "): %s", mId, aSdp);\n',
        '    LOG_INFO("Candidate (%" PRIu64 "): %s", mId, aSdp);\n'
        '    horizon_diag("ice.local_candidate", mId, 0, aSdp);\n',
    )

    replace_once(
        peer,
        '    LOG_INFO("Gathering done (%" PRIu64 ")", mId);\n',
        '    LOG_INFO("Gathering done (%" PRIu64 ")", mId);\n'
        '    horizon_diag("ice.gathering_done", mId);\n',
    )

    print(f"Injected Horizon diagnostics into {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
