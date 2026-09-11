#ifndef KPMHOOK_CONFIG_KPM_H
#define KPMHOOK_CONFIG_KPM_H

#include <stdbool.h>

#include "kpmhook/config-gfx.h"

/**
 * Check and bootstrap game.conf with optimal compatibility settings.
 *
 * @param gfx_cfg Graphics configuration obtained from cconfig.
 */
void kpm_config_bootstrap(const struct kpmhook_config_gfx *gfx_cfg);

/**
 * Check if sub-screen display should be rotated 90 degrees CCW (portrait mode).
 */
bool kpm_config_get_rotate(void);

#endif /* KPMHOOK_CONFIG_KPM_H */
