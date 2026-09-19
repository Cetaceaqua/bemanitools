#define LOG_MODULE "eamhook-eamuse"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eamhook/eamuse.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"
#include "util/net.h"

static int STDCALL my_connect(SOCKET s, const struct sockaddr *addr, int addrlen);
static struct hostent FAR *STDCALL my_gethostbyname(const char *name);
static int STDCALL my_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout);
static int STDCALL my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
static int STDCALL my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
static int STDCALL my_WSASendTo(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesSent,
    DWORD dwFlags,
    const struct sockaddr *lpTo,
    int iTolen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
static int STDCALL my_WSARecvFrom(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesRecvd,
    LPDWORD lpFlags,
    struct sockaddr *lpFrom,
    LPINT lpFromlen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

static int (STDCALL *real_connect)(SOCKET s, const struct sockaddr *addr, int addrlen);
static struct hostent FAR *(STDCALL *real_gethostbyname)(const char *name);
static int (STDCALL *real_select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout);
static int (STDCALL *real_sendto)(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
static int (STDCALL *real_recvfrom)(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
static int (STDCALL *real_WSASendTo)(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesSent,
    DWORD dwFlags,
    const struct sockaddr *lpTo,
    int iTolen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
static int (STDCALL *real_WSARecvFrom)(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesRecvd,
    LPDWORD lpFlags,
    struct sockaddr *lpFrom,
    LPINT lpFromlen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

static const struct hook_symbol eamuse_hook_syms[] = {
    {
        .name = "connect",
        .ordinal = 4,
        .patch = my_connect,
        .link = (void **) &real_connect,
    },
    {
        .name = "gethostbyname",
        .ordinal = 52,
        .patch = my_gethostbyname,
        .link = (void **) &real_gethostbyname,
    },
    {
        .name = "select",
        .ordinal = 18,
        .patch = my_select,
        .link = (void **) &real_select,
    },
    {
        .name = "sendto",
        .ordinal = 20,
        .patch = my_sendto,
        .link = (void **) &real_sendto,
    },
    {
        .name = "recvfrom",
        .ordinal = 17,
        .patch = my_recvfrom,
        .link = (void **) &real_recvfrom,
    },
    {
        .name = "WSASendTo",
        .ordinal = 99,
        .patch = my_WSASendTo,
        .link = (void **) &real_WSASendTo,
    },
    {
        .name = "WSARecvFrom",
        .ordinal = 93,
        .patch = my_WSARecvFrom,
        .link = (void **) &real_WSARecvFrom,
    },
};

static struct net_addr eamuse_server_addr;
static struct net_addr eamuse_server_addr_resolved;

/* Virtual SNTP responder state */
static SOCKET s_ntp_socket = INVALID_SOCKET;
static uint32_t s_ntp_orig_tm_s = 0;
static uint32_t s_ntp_orig_tm_f = 0;
static struct sockaddr_in s_ntp_server_addr;
static bool s_ntp_pending = false;

static bool is_eamuse_host(const char *name)
{
    if (!name) return false;
    /* Do not intercept local lookups */
    if (_stricmp(name, "127.0.0.1") == 0 || _stricmp(name, "localhost") == 0) {
        return false;
    }
    /* Resolve all game, service, and NTP hosts directly to the local eamuse server */
    return true;
}

static int STDCALL my_connect(SOCKET s, const struct sockaddr *addr, int addrlen)
{
    if (addr && addr->sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *) addr;
        char *ip_str = inet_ntoa(addr_in->sin_addr);
        uint16_t port = ntohs(addr_in->sin_port);

        if (addr_in->sin_addr.S_un.S_addr == eamuse_server_addr_resolved.ipv4.addr) {
            char *tmp = net_addr_to_str(&eamuse_server_addr_resolved);
            log_info(
                "Redirecting connect (%s:%d -> target %s)",
                ip_str,
                port,
                tmp);
            free(tmp);

            addr_in->sin_port = htons(eamuse_server_addr_resolved.ipv4.port);
        } else {
            log_misc("connect: %s:%d", ip_str, port);
        }
    }

    return real_connect(s, addr, addrlen);
}

static struct hostent FAR *STDCALL my_gethostbyname(const char *name)
{
    log_misc("gethostbyname: '%s'", name ? name : "<null>");

    if (!is_eamuse_host(name)) {
        return real_gethostbyname(name);
    }

    char *tmp = net_addr_to_str(&eamuse_server_addr_resolved);
    log_info("Resolving '%s' -> %s", name, tmp);
    free(tmp);

    static struct hostent ret;
    static uint32_t addr;
    static char *arr[2];
    static bool first = true;

    if (first) {
        ret.h_length = 4;
        ret.h_addrtype = AF_INET;
        ret.h_addr_list = (char **) &arr;
        ret.h_addr_list[0] = (char *) &addr;
        ret.h_addr_list[1] = NULL;
        ret.h_aliases = NULL;
        ret.h_name = NULL;
        first = false;
    }

    addr = eamuse_server_addr_resolved.ipv4.addr;
    return &ret;
}

static void ntp_handle_request(SOCKET s, const uint8_t *req, size_t req_len, const struct sockaddr_in *to)
{
    s_ntp_socket = s;
    s_ntp_pending = true;
    if (to) {
        memcpy(&s_ntp_server_addr, to, sizeof(struct sockaddr_in));
    }
    if (req && req_len >= 48) {
        /* Client transmit timestamp is at bytes 40..47 */
        memcpy(&s_ntp_orig_tm_s, req + 40, 4);
        memcpy(&s_ntp_orig_tm_f, req + 44, 4);
    }
}

static size_t ntp_create_response(uint8_t *resp, size_t max_len, struct sockaddr *from, int *fromlen)
{
    if (max_len < 48) {
        return 0;
    }

    memset(resp, 0, 48);
    resp[0] = 0x24; /* LI=0, VN=4, Mode=4 (server) */
    resp[1] = 1;    /* Stratum 1 */
    resp[2] = 4;    /* Poll */
    resp[3] = (uint8_t) -20; /* Precision */
    resp[12] = 'L'; resp[13] = 'O'; resp[14] = 'C'; resp[15] = 'L';

    time_t now_unix = time(NULL);
    /* 2208988800 is seconds between 1900-01-01 and 1970-01-01 */
    uint32_t ntp_sec = htonl((uint32_t)(now_unix + 2208988800ULL));

    memcpy(resp + 16, &ntp_sec, 4);        /* Reference timestamp */
    memcpy(resp + 24, &s_ntp_orig_tm_s, 4); /* Origin timestamp */
    memcpy(resp + 28, &s_ntp_orig_tm_f, 4);
    memcpy(resp + 32, &ntp_sec, 4);        /* Receive timestamp */
    memcpy(resp + 40, &ntp_sec, 4);        /* Transmit timestamp */

    if (from && fromlen && *fromlen >= (int) sizeof(struct sockaddr_in)) {
        memcpy(from, &s_ntp_server_addr, sizeof(struct sockaddr_in));
        *fromlen = sizeof(struct sockaddr_in);
    }

    s_ntp_pending = false;
    log_info("Synthesized valid SNTP response (0 ms latency)");
    return 48;
}

static int STDCALL my_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout)
{
    if (readfds && s_ntp_socket != INVALID_SOCKET && s_ntp_pending) {
        if (FD_ISSET(s_ntp_socket, readfds)) {
            /* If the NTP socket is the only one being polled, return immediately */
            if (readfds->fd_count == 1) {
                return 1;
            }

            /* Otherwise poll the other sockets with zero timeout */
            FD_CLR(s_ntp_socket, readfds);
            struct timeval tv_zero = {0, 0};
            int ret = real_select(nfds, readfds, writefds, exceptfds, &tv_zero);
            FD_SET(s_ntp_socket, readfds);
            return (ret >= 0) ? (ret + 1) : 1;
        }
    }

    return real_select(nfds, readfds, writefds, exceptfds, timeout);
}

static int STDCALL my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    if (to && to->sa_family == AF_INET) {
        const struct sockaddr_in *to_in = (const struct sockaddr_in *) to;
        if (ntohs(to_in->sin_port) == 123) {
            ntp_handle_request(s, (const uint8_t *) buf, len, to_in);
            return len;
        }
    }
    return real_sendto(s, buf, len, flags, to, tolen);
}

static int STDCALL my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    if (s == s_ntp_socket && s_ntp_pending) {
        size_t n = ntp_create_response((uint8_t *) buf, len, from, fromlen);
        if (n > 0) {
            return (int) n;
        }
    }
    return real_recvfrom(s, buf, len, flags, from, fromlen);
}

static int STDCALL my_WSASendTo(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesSent,
    DWORD dwFlags,
    const struct sockaddr *lpTo,
    int iTolen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    if (lpTo && lpTo->sa_family == AF_INET) {
        const struct sockaddr_in *to_in = (const struct sockaddr_in *) lpTo;
        if (ntohs(to_in->sin_port) == 123) {
            const uint8_t *req = (dwBufferCount > 0) ? (const uint8_t *) lpBuffers[0].buf : NULL;
            size_t req_len = (dwBufferCount > 0) ? lpBuffers[0].len : 0;
            ntp_handle_request(s, req, req_len, to_in);
            if (lpNumberOfBytesSent) {
                *lpNumberOfBytesSent = (DWORD) req_len;
            }
            return 0;
        }
    }
    return real_WSASendTo(
        s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped, lpCompletionRoutine);
}

static int STDCALL my_WSARecvFrom(
    SOCKET s,
    LPWSABUF lpBuffers,
    DWORD dwBufferCount,
    LPDWORD lpNumberOfBytesRecvd,
    LPDWORD lpFlags,
    struct sockaddr *lpFrom,
    LPINT lpFromlen,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    if (s == s_ntp_socket && s_ntp_pending) {
        if (dwBufferCount > 0 && lpBuffers[0].buf && lpBuffers[0].len >= 48) {
            int flen = (lpFromlen) ? *lpFromlen : 0;
            size_t n = ntp_create_response((uint8_t *) lpBuffers[0].buf, lpBuffers[0].len, lpFrom, &flen);
            if (lpFromlen) {
                *lpFromlen = flen;
            }
            if (lpNumberOfBytesRecvd) {
                *lpNumberOfBytesRecvd = (DWORD) n;
            }
            if (lpFlags) {
                *lpFlags = 0;
            }
            return 0;
        }
    }
    return real_WSARecvFrom(
        s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
}

void eamuse_hook_init(void)
{
    hook_table_apply(
        NULL, "ws2_32.dll", eamuse_hook_syms, lengthof(eamuse_hook_syms));
    log_info("Installed E-Amusement network redirection hooks on ws2_32.dll");
}

void eamuse_set_addr(const struct net_addr *addr)
{
    char *tmp_str;
    char *tmp_str2;

    log_assert(addr);
    memcpy(&eamuse_server_addr, addr, sizeof(struct net_addr));

    tmp_str = net_addr_to_str(&eamuse_server_addr);
    eamuse_server_addr_resolved.type = NET_ADDR_TYPE_IPV4;

    if (!net_resolve_hostname_net_addr(
            &eamuse_server_addr, &eamuse_server_addr_resolved.ipv4)) {
        log_fatal("Failed to resolve eamuse server address: %s", tmp_str);
        free(tmp_str);
        return;
    }

    if (eamuse_server_addr_resolved.ipv4.port == NET_INVALID_PORT) {
        log_info("No port specified in eamhook.conf, using port 80 as fallback");
        eamuse_server_addr_resolved.ipv4.port = 80;
    }

    tmp_str2 = net_addr_to_str(&eamuse_server_addr_resolved);
    log_info("E-Amusement server set to %s (target endpoint: %s)", tmp_str, tmp_str2);
    free(tmp_str);
    free(tmp_str2);
}

