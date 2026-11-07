/*
 * The UDP layer for netplay (net.h): BSD sockets, or Winsock on Windows; IPv6
 * with IPv4 on one dual-stack socket (IPV6_V6ONLY off), or IPv4 alone where
 * the system has no IPv6. Also the one other system call netplay needs
 * (netSelfPath): the system headers stay here, away from the game's (windows.h
 * has its own RECT).
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* getaddrinfo under a strict -std= (as cd/disc.c) */
#endif
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* inet_ntop, inet_pton: Vista and later */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
typedef SOCKET NetHandle;
#define NET_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif
typedef int NetHandle;
#define NET_BAD (-1)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

struct NetSocket {
    NetHandle h;
    int v6; /* a dual-stack IPv6 socket; else IPv4 only */
};

static char lastError[160];
static const uint8_t V4_PREFIX[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};

static void setError(const char* what) {
#ifdef _WIN32
    snprintf(lastError, sizeof lastError, "%s: Winsock error %d", what, WSAGetLastError());
#else
    snprintf(lastError, sizeof lastError, "%s: %s", what, strerror(errno));
#endif
}

const char* netError(void) {
    return lastError;
}

int netInit(void) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        setError("WSAStartup");
        return 0;
    }
#endif
    return 1;
}

void netQuit(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void closeHandle(NetHandle h) {
#ifdef _WIN32
    closesocket(h);
#else
    close(h);
#endif
}

/* A bound socket of one family; NET_BAD on failure. */
static NetHandle openFamily(int v6, int port) {
    NetHandle h = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (h == NET_BAD) {
        setError("socket");
        return NET_BAD;
    }
    int bound;
    if (v6) {
        int off = 0; /* IPv4 too, as ::ffff:a.b.c.d */
        setsockopt(h, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
        a.sin6_port = htons((unsigned short)port);
        bound = bind(h, (struct sockaddr*)&a, sizeof a) == 0;
    } else {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((unsigned short)port);
        bound = bind(h, (struct sockaddr*)&a, sizeof a) == 0;
    }
    if (!bound) {
        setError("bind");
        closeHandle(h);
        return NET_BAD;
    }
    return h;
}

NetSocket* netOpen(int port) {
    int v6 = 1;
    NetHandle h = openFamily(1, port);
    if (h == NET_BAD) {
        char why[sizeof lastError];
        memcpy(why, lastError, sizeof why);
        v6 = 0;
        h = openFamily(0, port);
        if (h == NET_BAD) {
            memcpy(lastError, why, sizeof why); /* IPv6's reason: the first tried */
            return NULL;
        }
    }
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(h, FIONBIO, &on);
    /* Winsock otherwise fails the next recvfrom after a datagram to a closed
     * port (an ICMP answer): the other side's restart is not an error here. */
    BOOL report = FALSE;
    DWORD unused = 0;
    WSAIoctl(h, SIO_UDP_CONNRESET, &report, sizeof report, NULL, 0, &unused, NULL, NULL);
#else
    fcntl(h, F_SETFL, fcntl(h, F_GETFL, 0) | O_NONBLOCK);
#endif
    NetSocket* s = calloc(1, sizeof *s);
    if (s == NULL) {
        closeHandle(h);
        return NULL;
    }
    s->h = h;
    s->v6 = v6;
    return s;
}

void netClose(NetSocket* s) {
    if (s == NULL) {
        return;
    }
    closeHandle(s->h);
    free(s);
}

int netLocalPort(NetSocket* s) {
    struct sockaddr_storage a;
#ifdef _WIN32
    int n = sizeof a;
#else
    socklen_t n = sizeof a;
#endif
    if (getsockname(s->h, (struct sockaddr*)&a, &n) != 0) {
        return 0;
    }
    return a.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6*)&a)->sin6_port)
                                   : ntohs(((struct sockaddr_in*)&a)->sin_port);
}

int netSocketV6(const NetSocket* s) {
    return s->v6;
}

int netAddrIsV4(const NetAddr* a) {
    return memcmp(a->ip, V4_PREFIX, sizeof V4_PREFIX) == 0;
}

static void fromV4(NetAddr* out, const struct in_addr* v4, int port) {
    memcpy(out->ip, V4_PREFIX, sizeof V4_PREFIX);
    memcpy(out->ip + 12, v4, 4);
    out->port = (uint16_t)port;
}

void netLoopback(NetAddr* out, int v6, int port) {
    memset(out, 0, sizeof *out);
    if (v6) {
        out->ip[15] = 1;
        out->port = (uint16_t)port;
    } else {
        struct in_addr a;
        a.s_addr = htonl(INADDR_LOOPBACK);
        fromV4(out, &a, port);
    }
}

int netResolve(const char* host, int port, NetAddr* out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
        snprintf(lastError, sizeof lastError, "cannot resolve %.100s", host);
        return 0;
    }
    memset(out, 0, sizeof *out);
    if (res->ai_family == AF_INET6) {
        memcpy(out->ip, &((struct sockaddr_in6*)res->ai_addr)->sin6_addr, 16);
        out->port = (uint16_t)port;
    } else {
        fromV4(out, &((struct sockaddr_in*)res->ai_addr)->sin_addr, port);
    }
    freeaddrinfo(res);
    return 1;
}

int netResolveHostPort(const char* text, NetAddr* out) {
    char host[256];
    const char* end;
    const char* colon;
    if (text[0] == '[') { /* [ipv6]:port */
        end = strchr(text, ']');
        colon = end != NULL && end[1] == ':' ? end + 1 : NULL;
        text++;
    } else {
        colon = strrchr(text, ':');
        end = colon;
        if (colon != NULL && strchr(text, ':') != colon) colon = NULL; /* an IPv6 address needs [] with a port */
    }
    if (colon == NULL || end == text || (size_t)(end - text) >= sizeof host) {
        snprintf(lastError, sizeof lastError, "%.100s: expected host:port or [ipv6]:port", text);
        return 0;
    }
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) {
        snprintf(lastError, sizeof lastError, "%.100s: bad port", text);
        return 0;
    }
    memcpy(host, text, (size_t)(end - text));
    host[end - text] = '\0';
    return netResolve(host, port, out);
}

int netSend(NetSocket* s, const NetAddr* to, const void* data, int size) {
    if (s->v6) {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        memcpy(&a.sin6_addr, to->ip, 16);
        a.sin6_port = htons(to->port);
        return sendto(s->h, (const char*)data, size, 0, (struct sockaddr*)&a, sizeof a) == size;
    }
    if (!netAddrIsV4(to)) {
        return 0; /* an IPv6 peer, an IPv4 socket */
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr, to->ip + 12, 4);
    a.sin_port = htons(to->port);
    return sendto(s->h, (const char*)data, size, 0, (struct sockaddr*)&a, sizeof a) == size;
}

int netRecv(NetSocket* s, NetAddr* from, void* data, int cap) {
    struct sockaddr_storage a;
#ifdef _WIN32
    int n = sizeof a;
#else
    socklen_t n = sizeof a;
#endif
    int got = (int)recvfrom(s->h, (char*)data, cap, 0, (struct sockaddr*)&a, &n);
    if (got < 0) {
        return -1;
    }
    memset(from, 0, sizeof *from);
    if (a.ss_family == AF_INET6) {
        memcpy(from->ip, &((struct sockaddr_in6*)&a)->sin6_addr, 16);
        from->port = ntohs(((struct sockaddr_in6*)&a)->sin6_port);
    } else {
        fromV4(from, &((struct sockaddr_in*)&a)->sin_addr, ntohs(((struct sockaddr_in*)&a)->sin_port));
    }
    return got;
}

int netWait(NetSocket* s, int ms) {
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET(s->h, &set);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select((int)s->h + 1, &set, NULL, NULL, &tv) > 0;
}

int netAddrEqual(const NetAddr* a, const NetAddr* b) {
    return memcmp(a->ip, b->ip, 16) == 0 && a->port == b->port;
}

void netAddrText(const NetAddr* a, char* out, int size) {
    char ip[64] = "?";
    if (netAddrIsV4(a)) {
        snprintf(out, (size_t)size, "%u.%u.%u.%u:%u", a->ip[12], a->ip[13], a->ip[14], a->ip[15], a->port);
        return;
    }
    inet_ntop(AF_INET6, a->ip, ip, sizeof ip);
    snprintf(out, (size_t)size, "[%s]:%u", ip, a->port);
}

int netSelfPath(char* out, int size) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size); /* UTF-8: the manifest's code page */
    return n > 0 && n < (DWORD)size;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)size;
    return _NSGetExecutablePath(out, &n) == 0;
#else
    snprintf(out, (size_t)size, "/proc/self/exe");
    return 1;
#endif
}
