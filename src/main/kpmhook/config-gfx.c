#include <string.h>

#include "cconfig/cconfig-util.h"
#include "kpmhook/config-gfx.h"
#include "util/log.h"

#define KPMHOOK_CONFIG_GFX_WINDOWED_KEY "gfx.windowed"
#define KPMHOOK_CONFIG_GFX_ROTATE_KEY   "gfx.rotate"

#define KPMHOOK_CONFIG_GFX_DEFAULT_WINDOWED_VALUE true
#define KPMHOOK_CONFIG_GFX_DEFAULT_ROTATE_VALUE   true

void kpmhook_config_gfx_init(struct cconfig *config)
{
    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_GFX_WINDOWED_KEY,
        KPMHOOK_CONFIG_GFX_DEFAULT_WINDOWED_VALUE,
        "Run the game in windowed mode (separate main/sub screen windows)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_GFX_ROTATE_KEY,
        KPMHOOK_CONFIG_GFX_DEFAULT_ROTATE_VALUE,
        "Rotate sub-screen 90 degrees counter-clockwise (portrait 768x1024)");
}

void kpmhook_config_gfx_get(
    struct kpmhook_config_gfx *config_gfx, struct cconfig *config)
{
    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_GFX_WINDOWED_KEY,
            &config_gfx->windowed,
            KPMHOOK_CONFIG_GFX_DEFAULT_WINDOWED_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_GFX_WINDOWED_KEY,
            KPMHOOK_CONFIG_GFX_DEFAULT_WINDOWED_VALUE);
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_GFX_ROTATE_KEY,
            &config_gfx->rotate,
            KPMHOOK_CONFIG_GFX_DEFAULT_ROTATE_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_GFX_ROTATE_KEY,
            KPMHOOK_CONFIG_GFX_DEFAULT_ROTATE_VALUE);
    }
}
