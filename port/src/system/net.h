#ifndef PORT_SYSTEM_NET_H
#define PORT_SYSTEM_NET_H

/*
 * A small UDP layer for netplay (netplay.c): datagrams through BSD sockets
 * (Linux, macOS) or Winsock (Windows), non-blocking, IPv6 and IPv4: one
 * dual-stack socket where the system has IPv6 (IPv4 peers arrive as
 * ::ffff:a.b.c.d), an IPv4 socket where it has not. SDL3 has no network
 * module.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t ip[16]; /* IPv6; IPv4 as ::ffff:a.b.c.d */
    uint16_t port;  /* host byte order */
} NetAddr;

typedef struct NetSocket NetSocket;

int netInit(void); /* 1 on success (Winsock's start; nothing elsewhere) */
void netQuit(void);

/* A UDP socket bound to `port` on every interface (0: any port), non-blocking:
 * IPv6 and IPv4 where the system can, else IPv4. NULL on failure (netError
 * says why). */
NetSocket* netOpen(int port);
void netClose(NetSocket* s);
int netLocalPort(NetSocket* s);
int netSocketV6(const NetSocket* s); /* 1: IPv6 and IPv4; 0: IPv4 only */

/* An address: "a.b.c.d", an IPv6 address ("2001:db8::1") or a host name,
 * and a port: 1 on success. */
int netResolve(const char* host, int port, NetAddr* out);
/* "host:port", "a.b.c.d:port" or "[ipv6]:port": 1 on success. */
int netResolveHostPort(const char* text, NetAddr* out);
/* The loopback address (::1, or 127.0.0.1 when v4) and a port: tests. */
void netLoopback(NetAddr* out, int v6, int port);
int netAddrIsV4(const NetAddr* a);

/* 1 when the datagram went out. */
int netSend(NetSocket* s, const NetAddr* to, const void* data, int size);
/* A waiting datagram: its size (at most `cap`, a larger one is cut), or -1
 * when none is waiting. */
int netRecv(NetSocket* s, NetAddr* from, void* data, int cap);
/* Wait up to `ms` milliseconds for a datagram: 1 when one is waiting. */
int netWait(NetSocket* s, int ms);

int netAddrEqual(const NetAddr* a, const NetAddr* b);
void netAddrText(const NetAddr* a, char* out, int size); /* "a.b.c.d:port" or "[ipv6]:port" */
const char* netError(void);

/* This program's own file (netplay hashes it when the build has no
 * compatibility id): 1 on success. */
int netSelfPath(char* out, int size);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_NET_H */
