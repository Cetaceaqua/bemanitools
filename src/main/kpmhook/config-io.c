#include <string.h>

#include "cconfig/cconfig-util.h"
#include "kpmhook/config-io.h"
#include "util/log.h"

#define KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY \
    "input.disable_debug_keys"
#define KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE \
    true

void kpmhook_config_io_init(struct cconfig *config)
{
    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE,
        "Disable game built-in developer debug keyboard shortcuts to prevent "
        "input collisions with arcade PCSub controls (default: true)");
}

void kpmhook_config_io_get(
    struct kpmhook_config_io *config_io, struct cconfig *config)
{
    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY,
            &config_io->disable_debug_keys,
            KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE);
    }
}
