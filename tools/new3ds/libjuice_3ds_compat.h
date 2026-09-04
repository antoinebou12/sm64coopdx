#pragma once

/*
 * Minimal IPv6 socket types for libjuice on New 3DS.
 * libctru exposes IPv4-only BSD sockets; CoopNet forces IPv4 ICE on 3DS.
 */
#ifdef __3DS__

#include <stdint.h>
#include <sys/socket.h>

#ifndef _SOCKADDR_IN6_DECLARED
#define _SOCKADDR_IN6_DECLARED
struct in6_addr {
    uint8_t s6_addr[16];
};
struct sockaddr_in6 {
    sa_family_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif

#endif /* __3DS__ */
