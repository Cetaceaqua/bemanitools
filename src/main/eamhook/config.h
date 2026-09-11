#ifndef EAMHOOK_CONFIG_H
#define EAMHOOK_CONFIG_H

#include <stdint.h>
#include "cconfig/cconfig.h"
#include "util/net.h"

struct eamhook_config {
    struct net_addr server_addr;
};

void eamhook_config_init(struct cconfig *config);
void eamhook_config_get(struct eamhook_config *cfg, struct cconfig *config);

#endif /* EAMHOOK_CONFIG_H */
