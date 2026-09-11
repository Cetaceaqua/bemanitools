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
    /* 5 Console Button Lamps (Offsets 356..360) */
    KPM_IO_LAMP_1BET           = (1 << 0),  /* 1 Bet button lamp (offset 356) */
    KPM_IO_LAMP_MAXBET         = (1 << 1),  /* Max Bet button lamp (offset 357) */
    KPM_IO_LAMP_START_REPEAT   = (1 << 2),  /* Start / Repeat button lamp (offset 358) */
    KPM_IO_LAMP_COLLECT_PAYOUT = (1 << 3),  /* Collect / Payout button lamp (offset 359) */
    KPM_IO_LAMP_TRANSFER       = (1 << 4),  /* Transfer button lamp (offset 360) */

    /* 6 Top Billboard Marquee Lamps (Offsets 361..366) */
    KPM_IO_LAMP_POP_L_RED      = (1 << 5),  /* Billboard POP LED Left Red (offset 361) */
    KPM_IO_LAMP_POP_L_GREEN    = (1 << 6),  /* Billboard POP LED Left Green (offset 362) */
    KPM_IO_LAMP_POP_L_BLUE     = (1 << 7),  /* Billboard POP LED Left Blue (offset 363) */
    KPM_IO_LAMP_POP_R_RED      = (1 << 8),  /* Billboard POP LED Right Red (offset 364) */
    KPM_IO_LAMP_POP_R_GREEN    = (1 << 9),  /* Billboard POP LED Right Green (offset 365) */
    KPM_IO_LAMP_POP_R_BLUE     = (1 << 10), /* Billboard POP LED Right Blue (offset 366) */

    /* 12 Normalized Cabinet Zone RGB Illumination Channels */
    KPM_IO_LAMP_SCREEN_R       = (1 << 11), /* Screen LED Red */
    KPM_IO_LAMP_SCREEN_G       = (1 << 12), /* Screen LED Green */
    KPM_IO_LAMP_SCREEN_B       = (1 << 13), /* Screen LED Blue */
    KPM_IO_LAMP_FRONT_R        = (1 << 14), /* Front Pillars LED Red */
    KPM_IO_LAMP_FRONT_G        = (1 << 15), /* Front Pillars LED Green */
    KPM_IO_LAMP_FRONT_B        = (1 << 16), /* Front Pillars LED Blue */
    KPM_IO_LAMP_SIDE_L_R       = (1 << 17), /* Side Left Edge LED Red */
    KPM_IO_LAMP_SIDE_L_G       = (1 << 18), /* Side Left Edge LED Green */
    KPM_IO_LAMP_SIDE_L_B       = (1 << 19), /* Side Left Edge LED Blue */
    KPM_IO_LAMP_SIDE_R_R       = (1 << 20), /* Side Right Edge LED Red */
    KPM_IO_LAMP_SIDE_R_G       = (1 << 21), /* Side Right Edge LED Green */
    KPM_IO_LAMP_SIDE_R_B       = (1 << 22), /* Side Right Edge LED Blue */
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
