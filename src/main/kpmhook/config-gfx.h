#ifndef KPMHOOK_CONFIG_GFX_H
#define KPMHOOK_CONFIG_GFX_H

#include <stdbool.h>

#include "cconfig/cconfig.h"

/**
 * Struct holding configuration values for GFX related items.
 */
struct kpmhook_config_gfx {
    bool windowed;
    bool rotate;
};

/**
 * Initialize a cconfig structure with the basic structure and default values
 * of this configuration.
 */
void kpmhook_config_gfx_init(struct cconfig *config);

/**
 * Read the module specific config struct values from the provided cconfig
 * struct.
 *
 * @param config_gfx Target module specific struct to read configuration
 *                   values to.
 * @param config cconfig struct holding the intermediate data to read from.
 */
void kpmhook_config_gfx_get(
    struct kpmhook_config_gfx *config_gfx, struct cconfig *config);

#endif /* KPMHOOK_CONFIG_GFX_H */
