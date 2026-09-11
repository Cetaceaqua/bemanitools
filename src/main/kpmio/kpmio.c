#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bemanitools/input.h"
#include "bemanitools/kpmio.h"

static log_formatter_t s_log_misc = NULL;
static log_formatter_t s_log_info = NULL;

static uint16_t s_buttons = 0;
static uint16_t s_coin_pulses = 0;
static bool s_coin_key_last = false;

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
    s_coin_pulses = 0;
    s_coin_key_last = false;
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

    /* Medal In: mapped bit 9 (rising edge triggers 1 medal pulse) */
    bool coin_key = (pack & (1ULL << 9)) != 0;
    if (coin_key && !s_coin_key_last) {
        s_coin_pulses++;
        if (s_log_misc) {
            s_log_misc("kpmio", "Medal inserted (pending pulses: %u)", s_coin_pulses);
        }
    }
    s_coin_key_last = coin_key;

    /* Virtual Hopper payout timer update */
    if (s_hopper_state == 1) {
        DWORD now = GetTickCount();
        if (now - s_last_hopper_tick >= 100) { /* 1 medal every 100ms */
            s_last_hopper_tick = now;
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
    for (uint8_t i = 0; i < 23; i++) {
        mapper_write_light(i, (lamp_bits & (1 << i)) ? 255 : 0);
    }
}
