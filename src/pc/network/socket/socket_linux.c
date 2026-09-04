#ifndef WINSOCK
#include "socket_linux.h"
#include "../network.h"
#include "pc/debuglog.h"
#if defined(__3DS__)
#include "pc/platform/new3ds/new3ds_runtime.h"
#include "pc/platform/new3ds/new3ds_log.h"
#endif

SOCKET socket_initialize(void) {
#if defined(__3DS__)
    if (!new3ds_runtime_ensure_network() || !new3ds_runtime_network_available()) {
        NEW3DS_LOG_ERROR_CAT(NEW3DS_LOG_CAT_NET, "socket", "SOC unavailable");
        LOG_ERROR("New 3DS Direct: socket unavailable (SOC not initialized)");
        return INVALID_SOCKET;
    }
#endif
#if defined(__SWITCH__) || defined(__3DS__)
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("socket failed with error %d", SOCKET_LAST_ERROR);
        return INVALID_SOCKET;
    }

    int rc = fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    if (rc == (int)INVALID_SOCKET) {
        LOG_ERROR("fcntl failed with error: %d", rc);
        closesocket(sock);
        return INVALID_SOCKET;
    }

#if defined(__SWITCH__)
    LOG_INFO("Switch Direct: IPv4 UDP socket initialized");
#elif defined(__3DS__)
    LOG_INFO("New 3DS Direct: IPv4 UDP socket initialized");
#else
    int v6only = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&v6only, sizeof(v6only)) < 0) {
        LOG_ERROR("setsockopt(IPV6_V6ONLY) failed.");
        closesocket(sock);
        return INVALID_SOCKET;
    }
#endif

    LOG_INFO("socket initialized.");
    return sock;
}

void socket_shutdown(SOCKET socket) {
    if (socket == INVALID_SOCKET) { return; }
    int rc = closesocket(socket);
    if (rc == (int)SOCKET_ERROR) {
        LOG_ERROR("closesocket failed with error %d\n", SOCKET_LAST_ERROR);
    }
}

#endif
