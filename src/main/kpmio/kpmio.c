#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bemanitools/input.h"
#include "bemanitools/kpmio.h"

static log_formatter_t s_log_misc = NULL;
static log_formatter_t s_log_info = NULL;

static uint16_t s_buttons = 0;
static uint16_t s_medal_pulses = 0;
static bool s_medal_key_last = false;
static DWORD s_medal_press_start_tick = 0;
static DWORD s_medal_last_repeat_tick = 0;

static uint16_t s_coin_pulses = 0;
static bool s_coin_key_last = false;
static DWORD s_coin_press_start_tick = 0;
static DWORD s_coin_last_repeat_tick = 0;

/* Virtual Hopper state */
static uint16_t s_payout_demanded = 0;
static uint16_t s_payout_paid = 0;
static uint8_t s_hopper_state = 0; /* 0=Idle, 1=Paying out, 2=Finished */
static DWORD s_last_hopper_tick = 0;

void kpm_io_set_loggers(
    log_formatter_t misc,
    log_formatter_t info,
    log_formatter_t warning,
    log_formatter_t fatal)
{
    s_log_misc = misc;
    s_log_info = info;

    input_set_loggers(misc, info, warning, fatal);
}

bool kpm_io_init(
    thread_create_t thread_create,
    thread_join_t thread_join,
    thread_destroy_t thread_destroy)
{
    if (s_log_info) {
        s_log_info("kpmio", "kpmio backend initializing...");
    }

    input_init(thread_create, thread_join, thread_destroy);
    mapper_config_load("kpm");

    s_buttons = 0;
    s_medal_pulses = 0;
    s_medal_key_last = false;
    s_medal_press_start_tick = 0;
    s_medal_last_repeat_tick = 0;
    s_coin_pulses = 0;
    s_coin_key_last = false;
    s_coin_press_start_tick = 0;
    s_coin_last_repeat_tick = 0;
    s_hopper_state = 0;
    s_payout_demanded = 0;
    s_payout_paid = 0;

    if (s_log_info) {
        s_log_info("kpmio", "kpmio backend initialized successfully");
    }
    return true;
}

void kpm_io_fini(void)
{
    input_fini();
    if (s_log_info) {
        s_log_info("kpmio", "kpmio backend shutdown");
    }
}

bool kpm_io_read_inputs(void)
{
    uint64_t pack = mapper_update();
    s_buttons = (uint16_t) (pack & 0x01FF); /* Bits 0..8 configured via config.exe */

    /* Medal In: mapped bit 9 (rising edge triggers 1 medal pulse, hold repeats) */
    bool medal_key = (pack & (1ULL << 9)) != 0;
    DWORD now = GetTickCount();

    if (medal_key) {
        if (!s_medal_key_last) {
            /* Initial press */
            s_medal_pulses++;
            s_medal_press_start_tick = now;
            s_medal_last_repeat_tick = now;
            if (s_log_misc) {
                s_log_misc("kpmio", "Medal inserted (initial, pending: %u)", s_medal_pulses);
            }
        } else {
            /* Held down: after 350ms initial delay, repeat every 80ms (~12.5 medals/sec) */
            if ((now - s_medal_press_start_tick >= 350) && (now - s_medal_last_repeat_tick >= 80)) {
                s_medal_pulses++;
                s_medal_last_repeat_tick = now;
                if (s_log_misc) {
                    s_log_misc("kpmio", "Medal inserted (autofire repeat, pending: %u)", s_medal_pulses);
                }
            }
        }
    }
    s_medal_key_last = medal_key;

    /* Coin In: mapped bit 10 (100-yen coin insertion, hold repeats) */
    bool coin_key = (pack & (1ULL << 10)) != 0;

    if (coin_key) {
        if (!s_coin_key_last) {
            /* Initial press */
            s_coin_pulses++;
            s_coin_press_start_tick = now;
            s_coin_last_repeat_tick = now;
            if (s_log_misc) {
                s_log_misc("kpmio", "Coin (100 Yen) inserted (initial, pending: %u)", s_coin_pulses);
            }
        } else {
            /* Held down: after 350ms initial delay, repeat every 100ms (~10 coins/sec) */
            if ((now - s_coin_press_start_tick >= 350) && (now - s_coin_last_repeat_tick >= 100)) {
                s_coin_pulses++;
                s_coin_last_repeat_tick = now;
                if (s_log_misc) {
                    s_log_misc("kpmio", "Coin (100 Yen) inserted (repeat, pending: %u)", s_coin_pulses);
                }
            }
        }
    }
    s_coin_key_last = coin_key;

    /* Virtual Hopper payout timer update */
    if (s_hopper_state == 1) {
        DWORD now_hopper = GetTickCount();
        if (now_hopper - s_last_hopper_tick >= 100) { /* 1 medal every 100ms */
            s_last_hopper_tick = now_hopper;
            s_payout_paid++;
            if (s_payout_paid >= s_payout_demanded) {
                s_hopper_state = 2; /* Finished */
                if (s_log_info) {
                    s_log_info("kpmio", "Hopper payout complete: %u medals dispensed", s_payout_paid);
                }
            }
        }
    }

    return true;
}

uint16_t kpm_io_get_buttons(void)
{
    return s_buttons;
}

uint16_t kpm_io_get_medal_pulse(void)
{
    uint16_t p = s_medal_pulses;
    s_medal_pulses = 0;
    return p;
}

uint16_t kpm_io_get_coin_pulse(void)
{
    uint16_t p = s_coin_pulses;
    s_coin_pulses = 0;
    return p;
}

void kpm_io_payout_demand(uint16_t count)
{
    s_payout_demanded = count;
    s_payout_paid = 0;
    s_hopper_state = (count > 0) ? 1 : 0;
    s_last_hopper_tick = GetTickCount();

    if (s_log_info) {
        s_log_info("kpmio", "Hopper payout demand: %u medals", count);
    }
}

uint8_t kpm_io_get_payout_status(uint16_t *paid_count)
{
    if (paid_count) {
        *paid_count = s_payout_paid;
    }
    return s_hopper_state;
}

void kpm_io_set_lamps(uint32_t lamp_bits)
{
    static uint32_t s_last_bits = 0xFFFFFFFF;
    if (lamp_bits != s_last_bits) {
        if (s_log_info) {
            s_log_info("kpmio", "kpm_io_set_lamps: 0x%08X", lamp_bits);
        }
        s_last_bits = lamp_bits;
    }

    for (uint8_t i = 0; i < 23; i++) {
        mapper_write_light(i, (lamp_bits & (1 << i)) ? 255 : 0);
    }
}

