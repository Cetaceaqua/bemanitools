#define LOG_MODULE "eamhook-eamuse"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eamhook/eamuse.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"
#include "util/net.h"

static int STDCALL my_connect(SOCKET s, const struct sockaddr *addr, int addrlen);
static struct hostent FAR *STDCALL my_gethostbyname(const char *name);

static int (STDCALL *real_connect)(SOCKET s, const struct sockaddr *addr, int addrlen);
static struct hostent FAR *(STDCALL *real_gethostbyname)(const char *name);

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
};

static struct net_addr eamuse_server_addr;
static struct net_addr eamuse_server_addr_resolved;

static bool is_eamuse_host(const char *name)
{
    if (!name) return false;
    if (_stricmp(name, "services") == 0) return true;
    if (_stricmp(name, "services.eamuse.konami.fun") == 0) return true;
    if (strstr(name, "eamuse.konami.fun") != NULL) return true;
    if (strstr(name, "konami.fun") != NULL) return true;
    if (_strnicmp(name, "services.", 9) == 0) return true;
    return false;
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
