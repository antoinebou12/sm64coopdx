#ifndef WINSOCK
#include "socket_linux.h"
#include "../network.h"
#include "pc/debuglog.h"

SOCKET socket_initialize(void) {
#ifdef __SWITCH__
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

#ifndef __SWITCH__
    int v6only = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&v6only, sizeof(v6only)) < 0) {
        LOG_ERROR("setsockopt(IPV6_V6ONLY) failed.");
        closesocket(sock);
        return INVALID_SOCKET;
    }
#else
    LOG_INFO("Switch Direct: IPv4 UDP socket initialized");
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
