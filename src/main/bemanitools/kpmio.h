#ifndef BEMANITOOLS_KPMIO_H
#define BEMANITOOLS_KPMIO_H

/* IO emulation provider for LovePlus MEDAL Happy Daily Life (KPM). */

#include <stdbool.h>
#include <stdint.h>

#include "bemanitools/glue.h"

/**
 * Bit mappings for the arcade cabinet buttons and switches.
 * Directly corresponds to the hardware bitmask read by the game
 * at dword_1ACFCB8 + 336 (virtual channels 0..11).
 */
enum kpm_io_button_bit {
    KPM_IO_BTN_TEST           = (1 << 0),  /* Test switch (virtual channel 0) */
    KPM_IO_BTN_1BET           = (1 << 1),  /* 1 Bet button (virtual channel 2) */
    KPM_IO_BTN_MAXBET         = (1 << 2),  /* Max Bet button (virtual channel 3) */
    KPM_IO_BTN_START_REPEAT   = (1 << 3),  /* Start / Repeat button (virtual channel 4) */
    KPM_IO_BTN_COLLECT_PAYOUT = (1 << 4),  /* Collect / Payout button (virtual channel 1) */
    KPM_IO_BTN_RESET_KEY      = (1 << 5),  /* Attendant reset key (virtual channel 6) */
    KPM_IO_BTN_TRANSFER       = (1 << 6),  /* Transfer button (virtual channel 9) */
    KPM_IO_BTN_UPPER_SCREEN_L = (1 << 7),  /* Upper Screen L (virtual channel 10) */
    KPM_IO_BTN_UPPER_SCREEN_R = (1 << 8),  /* Upper Screen R (virtual channel 11) */
};

/**
 * Cabinet console and illumination lamps.
 */
enum kpm_io_lamp_bit {
    KPM_IO_LAMP_1BET           = (1 << 0),
    KPM_IO_LAMP_MAXBET         = (1 << 1),
    KPM_IO_LAMP_START_REPEAT   = (1 << 2),
    KPM_IO_LAMP_COLLECT_PAYOUT = (1 << 3),
    KPM_IO_LAMP_TRANSFER       = (1 << 4),
    KPM_IO_LAMP_UPPER_L        = (1 << 5),
    KPM_IO_LAMP_UPPER_R        = (1 << 6),
    KPM_IO_LAMP_HOPPER_ERROR   = (1 << 7),
};

/**
 * Supply logging functions to the IO DLL.
 */
void kpm_io_set_loggers(
    log_formatter_t misc,
    log_formatter_t info,
    log_formatter_t warning,
    log_formatter_t fatal);

/**
 * Initialize the KPM IO emulation subsystem.
 */
bool kpm_io_init(
    thread_create_t thread_create,
    thread_join_t thread_join,
    thread_destroy_t thread_destroy);

/**
 * Shut down the KPM IO emulation subsystem.
 */
void kpm_io_fini(void);

/**
 * Poll inputs from hardware / keyboard / controllers.
 */
bool kpm_io_read_inputs(void);

/**
 * Get the current button bitmask.
 *
 * @return Bitmask combining enum kpm_io_button_bit flags.
 */
uint16_t kpm_io_get_buttons(void);

/**
 * Get the number of coins/medals inserted since the last call.
 * This will increment dword_1ACFCB8 + 508.
 *
 * @return Number of new coin/medal pulses.
 */
uint16_t kpm_io_get_coin_pulse(void);

/**
 * Request payout from the virtual medal hopper.
 *
 * @param count Number of medals requested for payout.
 */
void kpm_io_payout_demand(uint16_t count);

/**
 * Query current virtual hopper status.
 *
 * @param[out] paid_count Cumulative paid medals count.
 * @return Hopper state (0: Idle, 1: Paying out, 2: Finished, 3: Error).
 */
uint8_t kpm_io_get_payout_status(uint16_t *paid_count);

/**
 * Set console button and cabinet illumination lamps.
 *
 * @param lamp_bits Bitmask of enum kpm_io_lamp_bit flags.
 */
void kpm_io_set_lamps(uint32_t lamp_bits);

#endif /* BEMANITOOLS_KPMIO_H */
