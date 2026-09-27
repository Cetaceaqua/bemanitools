#ifndef KPMHOOK_CONFIG_KPM_H
#define KPMHOOK_CONFIG_KPM_H

#include <stdbool.h>

#include "kpmhook/config-gfx.h"
#include "kpmhook/config-io.h"

/**
 * Check and bootstrap game.conf with optimal compatibility settings.
 *
 * @param gfx_cfg Graphics configuration obtained from cconfig.
 * @param io_cfg  IO configuration obtained from cconfig.
 */
void kpm_config_bootstrap(
    const struct kpmhook_config_gfx *gfx_cfg,
    const struct kpmhook_config_io *io_cfg);

/**
 * Check if sub-screen display should be rotated 90 degrees CCW (portrait mode).
 */
bool kpm_config_get_rotate(void);

/**
 * Check if sub-screen (Station 1) is enabled.
 */
bool kpm_config_get_sub_screen(void);

/**
 * Check if sub-screen touch / mouse IR emulation is enabled.
 */
bool kpm_config_get_sub_screen_touch(void);
bool kpm_config_get_upper_screen_touch(void);

#endif /* KPMHOOK_CONFIG_KPM_H */
