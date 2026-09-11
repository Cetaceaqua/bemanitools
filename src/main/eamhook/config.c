#include <string.h>
#include <stdlib.h>
#include "cconfig/cconfig-util.h"
#include "eamhook/config.h"
#include "util/log.h"
#include "util/net.h"

#define EAMHOOK_CONFIG_SERVER_KEY "server"
#define EAMHOOK_CONFIG_DEFAULT_SERVER_VALUE "127.0.0.1:8083"

static const struct net_addr eamhook_default_server = {
    .type = NET_ADDR_TYPE_HOSTNAME,
    .hostname.host = "127.0.0.1",
    .hostname.port = 8083,
};

void eamhook_config_init(struct cconfig *config)
{
    cconfig_util_set_str(
        config,
        EAMHOOK_CONFIG_SERVER_KEY,
        EAMHOOK_CONFIG_DEFAULT_SERVER_VALUE,
        "Target E-Amusement server address (e.g. 127.0.0.1:8083, localhost:8080, or http://127.0.0.1:8083/services/). Default: 127.0.0.1:8083");
}

void eamhook_config_get(struct eamhook_config *cfg, struct cconfig *config)
{
    char server_str[1024];

    if (!cconfig_util_get_str(
            config,
            EAMHOOK_CONFIG_SERVER_KEY,
            server_str,
            sizeof(server_str),
            EAMHOOK_CONFIG_DEFAULT_SERVER_VALUE)) {
        log_warning(
            "Invalid value for key '%s', using default '%s'",
            EAMHOOK_CONFIG_SERVER_KEY,
            EAMHOOK_CONFIG_DEFAULT_SERVER_VALUE);
    }

    if (!net_str_parse(server_str, &cfg->server_addr)) {
        memcpy(&cfg->server_addr, &eamhook_default_server, sizeof(cfg->server_addr));
        log_warning("Failed to parse server address '%s', using fallback 127.0.0.1:80", server_str);
    }
}
