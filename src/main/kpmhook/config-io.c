#include <string.h>

#include "cconfig/cconfig-util.h"
#include "kpmhook/config-io.h"
#include "util/log.h"

#define KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY \
    "input.disable_debug_keys"
#define KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE \
    true

#define KPMHOOK_CONFIG_IO_LIGHTS_NORMALIZED_KEY \
    "light.normalized"
#define KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_NORMALIZED_VALUE \
    true

#define KPMHOOK_CONFIG_IO_LIGHTS_RAW_SERIAL_KEY \
    "light.raw_serial"
#define KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_SERIAL_VALUE \
    false

#define KPMHOOK_CONFIG_IO_LIGHTS_RAW_PORT_KEY \
    "light.raw_serial_port"
#define KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_PORT_VALUE \
    "COM3"

#define KPMHOOK_CONFIG_IO_LIGHTS_RAW_BAUD_KEY \
    "light.raw_serial_baud"
#define KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_BAUD_VALUE \
    115200

#define KPMHOOK_CONFIG_IO_CARD_PORT_KEY \
    "card.port"
#define KPMHOOK_CONFIG_IO_DEFAULT_CARD_PORT_VALUE \
    ""

void kpmhook_config_io_init(struct cconfig *config)
{
    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_DISABLE_DEBUG_KEYS_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_DISABLE_DEBUG_KEYS_VALUE,
        "Disable game built-in developer debug keyboard shortcuts to prevent "
        "input collisions with arcade PCSub controls (default: true)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_LIGHTS_NORMALIZED_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_NORMALIZED_VALUE,
        "Normalize 110 cabinet LEDs into 4 zones (Screen, Front, Side L/R) for config.exe (default: true)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_LIGHTS_RAW_SERIAL_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_SERIAL_VALUE,
        "Stream raw 110-LED RGB lighting frames to serial port for DIY LED hardware (default: false)");

    cconfig_util_set_str(
        config,
        KPMHOOK_CONFIG_IO_LIGHTS_RAW_PORT_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_PORT_VALUE,
        "Serial port for DIY raw LED stream (default: COM3)");

    cconfig_util_set_int(
        config,
        KPMHOOK_CONFIG_IO_LIGHTS_RAW_BAUD_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_BAUD_VALUE,
        "Baud rate for DIY raw LED stream (default: 115200)");

    cconfig_util_set_str(
        config,
        KPMHOOK_CONFIG_IO_CARD_PORT_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_CARD_PORT_VALUE,
        "Serial port for real physical card reader pass-through (e.g. COM3). "
        "Leave empty to use eamio virtual card reader emulation (default: empty)");
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

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_LIGHTS_NORMALIZED_KEY,
            &config_io->lights_normalized,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_NORMALIZED_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_LIGHTS_NORMALIZED_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_NORMALIZED_VALUE);
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_SERIAL_KEY,
            &config_io->lights_raw_serial,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_SERIAL_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_SERIAL_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_SERIAL_VALUE);
    }

    if (!cconfig_util_get_str(
            config,
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_PORT_KEY,
            config_io->lights_raw_port,
            sizeof(config_io->lights_raw_port) - 1,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_PORT_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%s'",
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_PORT_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_PORT_VALUE);
    }

    if (!cconfig_util_get_int(
            config,
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_BAUD_KEY,
            &config_io->lights_raw_baud,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_BAUD_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_LIGHTS_RAW_BAUD_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_LIGHTS_RAW_BAUD_VALUE);
    }

    if (!cconfig_util_get_str(
            config,
            KPMHOOK_CONFIG_IO_CARD_PORT_KEY,
            config_io->card_port,
            sizeof(config_io->card_port) - 1,
            KPMHOOK_CONFIG_IO_DEFAULT_CARD_PORT_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%s'",
            KPMHOOK_CONFIG_IO_CARD_PORT_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_CARD_PORT_VALUE);
    }
}
