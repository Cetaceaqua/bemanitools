#ifndef KPMHOOK_CONFIG_IO_H
#define KPMHOOK_CONFIG_IO_H

#include <stdbool.h>

#include "cconfig/cconfig.h"

/**
 * Configuration values for input and I/O related items.
 */
enum kpmhook_cabinet_girl {
    KPMHOOK_CABINET_GIRL_MANAKA = 0,
    KPMHOOK_CABINET_GIRL_RINKO  = 1,
    KPMHOOK_CABINET_GIRL_NENE   = 2,
};

struct kpmhook_config_io {
    bool disable_debug_keys;
    bool lights_normalized;
    bool lights_raw_serial;
    char lights_raw_port[32];
    int lights_raw_baud;
    char card_port[32];
    enum kpmhook_cabinet_girl cabinet_girl;
    int boot_credits;
    uint8_t attract_mode;
    bool show_secret_menu;
    int attract_timeout;
    bool boot_to_title;
    bool coin_auto_transfer;
    bool play_movie;
};


/**
 * Initialize a cconfig structure with the basic structure and default values
 * of this configuration.
 */
void kpmhook_config_io_init(struct cconfig *config);

/**
 * Read the module specific config struct values from the provided cconfig
 * struct.
 *
 * @param config_io Target module specific struct to read configuration
 *                  values to.
 * @param config cconfig struct holding the intermediate data to read from.
 */
void kpmhook_config_io_get(
    struct kpmhook_config_io *config_io, struct cconfig *config);

#endif /* KPMHOOK_CONFIG_IO_H */
