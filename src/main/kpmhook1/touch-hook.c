#define LOG_MODULE "kpm-touch"

#include <windows.h>
#include <stdbool.h>

#include "hook/table.h"
#include "kpmhook1/touch-hook.h"
#include "util/defs.h"
#include "util/log.h"

static int __cdecl my_EloSetMouseMode(int mode, int screen)
{
    log_info("EloSetMouseMode(mode=%d, screen=%d)", mode, screen);
    return 0;
}

static int __cdecl my_EloSetTouchReportingState(int state, int screen)
{
    log_info("EloSetTouchReportingState(state=%d, screen=%d)", state, screen);
    return 0;
}

static int __cdecl my_EloGetScreenInfo(void *buf, unsigned int *num_screens)
{
    log_info("EloGetScreenInfo called -> reporting 0 screens");
    if (buf) {
        memset(buf, 0, 128);
    }
    if (num_screens) {
        *num_screens = 0;
    }
    return 0;
}

static int __cdecl my_EloGetTouch(int *touch_data, int a2, int a3, unsigned int *screen_idx)
{
    /* Non-zero return indicates no touch data available */
    return 1;
}

static int __cdecl my_EloCancel(void)
{
    log_info("EloCancel()");
    return 0;
}

static int __cdecl my_EloSetBeep(void *data, int screen)
{
    log_info("EloSetBeep(screen=%d)", screen);
    return 0;
}

static const struct hook_symbol kpm_elo_syms[] = {
    {
        .name = "EloSetMouseMode",
        .patch = my_EloSetMouseMode,
    },
    {
        .name = "EloSetTouchReportingState",
        .patch = my_EloSetTouchReportingState,
    },
    {
        .name = "EloGetScreenInfo",
        .patch = my_EloGetScreenInfo,
    },
    {
        .name = "EloGetTouch",
        .patch = my_EloGetTouch,
    },
    {
        .name = "EloCancel",
        .patch = my_EloCancel,
    },
    {
        .name = "EloSetBeep",
        .patch = my_EloSetBeep,
    },
};

void kpm_touch_hook_init(void)
{
    hook_table_apply(
        NULL,
        "EloPubIf.dll",
        kpm_elo_syms,
        lengthof(kpm_elo_syms));

    log_info("EloPubIf touch panel hooks installed");
}
