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

#define KPMHOOK_CONFIG_IO_CABINET_GIRL_KEY \
    "cabinet.girl"
#define KPMHOOK_CONFIG_IO_DEFAULT_CABINET_GIRL_VALUE \
    "manaka"

#define KPMHOOK_CONFIG_IO_BOOT_CREDITS_KEY \
    "game.boot_credits"
#define KPMHOOK_CONFIG_IO_DEFAULT_BOOT_CREDITS_VALUE \
    0

#define KPMHOOK_CONFIG_IO_ATTRACT_MODE_KEY \
    "game.attract_mode"
#define KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_MODE_VALUE \
    "alt"

#define KPMHOOK_CONFIG_IO_SHOW_SECRET_MENU_KEY \
    "game.show_secret_menu"
#define KPMHOOK_CONFIG_IO_DEFAULT_SHOW_SECRET_MENU_VALUE \
    true

#define KPMHOOK_CONFIG_IO_ATTRACT_TIMEOUT_KEY \
    "game.attract_timeout"
#define KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_TIMEOUT_VALUE \
    30

#define KPMHOOK_CONFIG_IO_BOOT_TO_TITLE_KEY \
    "game.boot_to_title"
#define KPMHOOK_CONFIG_IO_DEFAULT_BOOT_TO_TITLE_VALUE \
    true

#define KPMHOOK_CONFIG_IO_COIN_AUTO_TRANSFER_KEY \
    "coin.auto_transfer"
#define KPMHOOK_CONFIG_IO_DEFAULT_COIN_AUTO_TRANSFER_VALUE \
    false

#define KPMHOOK_CONFIG_IO_PLAY_MOVIE_KEY \
    "game.play_movie"
#define KPMHOOK_CONFIG_IO_DEFAULT_PLAY_MOVIE_VALUE \
    true


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

    cconfig_util_set_str(
        config,
        KPMHOOK_CONFIG_IO_CABINET_GIRL_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_CABINET_GIRL_VALUE,
        "Cabinet girlfriend jumper type (manaka, rinko, nene). Simulates PCSub "
        "hardware jumper plug to designate default girlfriend for guest play (default: manaka)");

    cconfig_util_set_int(
        config,
        KPMHOOK_CONFIG_IO_BOOT_CREDITS_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_BOOT_CREDITS_VALUE,
        "Starting credits on boot. 0 for authentic arcade operation (waiting for medals), "
        "1000 for developer free-play (default: 0)");

    cconfig_util_set_str(
        config,
        KPMHOOK_CONFIG_IO_ATTRACT_MODE_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_MODE_VALUE,
        "Standby attract mode when machine is idle: 'alt' (BOTH ALT: alternates between Slot and Panel demo), "
        "'panel' (BOTH PANEL), 'slot' (BOTH SLOT) (default: alt)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_SHOW_SECRET_MENU_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_SHOW_SECRET_MENU_VALUE,
        "Unlock and show SECRET MODE (Aging, Debug, Local Clock, etc.) directly in the arcade Test Menu (default: true)");

    cconfig_util_set_int(
        config,
        KPMHOOK_CONFIG_IO_ATTRACT_TIMEOUT_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_TIMEOUT_VALUE,
        "Idle timeout in seconds before transitioning to Title/Attract CM (default: 30, arcade original: 120)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_BOOT_TO_TITLE_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_BOOT_TO_TITLE_VALUE,
        "Boot directly into Title / Attract movie on game launch (default: true)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_COIN_AUTO_TRANSFER_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_COIN_AUTO_TRANSFER_VALUE,
        "Automatically transfer inserted 100-yen coins to game medals without pressing Transfer button (default: false)");

    cconfig_util_set_bool(
        config,
        KPMHOOK_CONFIG_IO_PLAY_MOVIE_KEY,
        KPMHOOK_CONFIG_IO_DEFAULT_PLAY_MOVIE_VALUE,
        "Enable opening WMV character attract movie playback in Title / Attract mode (default: true)");
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

    char girl_buf[32];
    if (cconfig_util_get_str(
            config,
            KPMHOOK_CONFIG_IO_CABINET_GIRL_KEY,
            girl_buf,
            sizeof(girl_buf) - 1,
            KPMHOOK_CONFIG_IO_DEFAULT_CABINET_GIRL_VALUE)) {
        if (!_stricmp(girl_buf, "rinko") || !strcmp(girl_buf, "1")) {
            config_io->cabinet_girl = KPMHOOK_CABINET_GIRL_RINKO;
        } else if (!_stricmp(girl_buf, "nene") || !strcmp(girl_buf, "2") || !strcmp(girl_buf, "3")) {
            config_io->cabinet_girl = KPMHOOK_CABINET_GIRL_NENE;
        } else {
            config_io->cabinet_girl = KPMHOOK_CABINET_GIRL_MANAKA;
        }
    } else {
        config_io->cabinet_girl = KPMHOOK_CABINET_GIRL_MANAKA;
    }

    if (!cconfig_util_get_int(
            config,
            KPMHOOK_CONFIG_IO_BOOT_CREDITS_KEY,
            &config_io->boot_credits,
            KPMHOOK_CONFIG_IO_DEFAULT_BOOT_CREDITS_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_BOOT_CREDITS_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_BOOT_CREDITS_VALUE);
    }

    char attract_buf[32];
    if (cconfig_util_get_str(
            config,
            KPMHOOK_CONFIG_IO_ATTRACT_MODE_KEY,
            attract_buf,
            sizeof(attract_buf) - 1,
            KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_MODE_VALUE)) {
        if (!_stricmp(attract_buf, "panel") || !strcmp(attract_buf, "1")) {
            config_io->attract_mode = 1; /* BOTH PANEL */
        } else if (!_stricmp(attract_buf, "slot") || !strcmp(attract_buf, "2")) {
            config_io->attract_mode = 2; /* BOTH SLOT */
        } else {
            config_io->attract_mode = 0; /* BOTH ALT (Default) */
        }
    } else {
        config_io->attract_mode = 0;
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_SHOW_SECRET_MENU_KEY,
            &config_io->show_secret_menu,
            KPMHOOK_CONFIG_IO_DEFAULT_SHOW_SECRET_MENU_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_SHOW_SECRET_MENU_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_SHOW_SECRET_MENU_VALUE);
    }

    if (!cconfig_util_get_int(
            config,
            KPMHOOK_CONFIG_IO_ATTRACT_TIMEOUT_KEY,
            &config_io->attract_timeout,
            KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_TIMEOUT_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_ATTRACT_TIMEOUT_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_ATTRACT_TIMEOUT_VALUE);
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_BOOT_TO_TITLE_KEY,
            &config_io->boot_to_title,
            KPMHOOK_CONFIG_IO_DEFAULT_BOOT_TO_TITLE_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_BOOT_TO_TITLE_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_BOOT_TO_TITLE_VALUE);
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_COIN_AUTO_TRANSFER_KEY,
            &config_io->coin_auto_transfer,
            KPMHOOK_CONFIG_IO_DEFAULT_COIN_AUTO_TRANSFER_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_COIN_AUTO_TRANSFER_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_COIN_AUTO_TRANSFER_VALUE);
    }

    if (!cconfig_util_get_bool(
            config,
            KPMHOOK_CONFIG_IO_PLAY_MOVIE_KEY,
            &config_io->play_movie,
            KPMHOOK_CONFIG_IO_DEFAULT_PLAY_MOVIE_VALUE)) {
        log_warning(
            "Invalid value for key '%s' specified, fallback to default '%d'",
            KPMHOOK_CONFIG_IO_PLAY_MOVIE_KEY,
            KPMHOOK_CONFIG_IO_DEFAULT_PLAY_MOVIE_VALUE);
    }
}

