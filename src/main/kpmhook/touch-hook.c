#define LOG_MODULE "kpm-touch"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hook/table.h"
#include "kpmhook/touch-hook.h"
#include "util/defs.h"
#include "util/log.h"

#define MAX_SCREENS 2
#define TOUCH_QUEUE_SIZE 64

struct elo_touch_event {
    int x;
    int y;
    int type; /* 1 = Down, 2 = Move, 4 = Up */
};

struct elo_screen_queue {
    CRITICAL_SECTION cs;
    struct elo_touch_event events[TOUCH_QUEUE_SIZE];
    int head;
    int tail;
    int last_x;
    int last_y;
    bool is_down;
};

static struct elo_screen_queue s_screens[MAX_SCREENS];
static bool s_touch_inited = false;

static void init_screen_queues(void)
{
    if (!s_touch_inited) {
        s_touch_inited = true;
        for (int i = 0; i < MAX_SCREENS; i++) {
            InitializeCriticalSection(&s_screens[i].cs);
            s_screens[i].head = 0;
            s_screens[i].tail = 0;
            s_screens[i].last_x = -1;
            s_screens[i].last_y = -1;
            s_screens[i].is_down = false;
        }
    }
}

void kpm_touch_post_event(int screen, int client_x, int client_y, int client_w, int client_h, int type)
{
    if (screen < 0 || screen >= MAX_SCREENS || client_w <= 0 || client_h <= 0) {
        return;
    }
    init_screen_queues();

    /* Convert client pixels to Elo 12-bit ADC raw range (0..4092) */
    int raw_x = (int) (((int64_t) client_x * 4092) / client_w);
    int raw_y = (int) (((int64_t) client_y * 4092) / client_h);

    if (raw_x < 0) raw_x = 0;
    if (raw_x > 4092) raw_x = 4092;
    if (raw_y < 0) raw_y = 0;
    if (raw_y > 4092) raw_y = 4092;

    struct elo_screen_queue *q = &s_screens[screen];
    EnterCriticalSection(&q->cs);

    if (type == 1) { /* Down */
        q->is_down = true;
        q->last_x = raw_x;
        q->last_y = raw_y;
    } else if (type == 2) { /* Move */
        if (!q->is_down) {
            LeaveCriticalSection(&q->cs);
            return;
        }
        /* Filter out duplicate coordinates to avoid flood */
        if (raw_x == q->last_x && raw_y == q->last_y) {
            LeaveCriticalSection(&q->cs);
            return;
        }
        q->last_x = raw_x;
        q->last_y = raw_y;
    } else if (type == 4) { /* Up */
        q->is_down = false;
        q->last_x = raw_x;
        q->last_y = raw_y;
    }

    int next_head = (q->head + 1) % TOUCH_QUEUE_SIZE;
    if (next_head != q->tail) {
        q->events[q->head].x = raw_x;
        q->events[q->head].y = raw_y;
        q->events[q->head].type = type;
        q->head = next_head;
    } else {
        /* Queue full: overwrite oldest event */
        q->events[q->head].x = raw_x;
        q->events[q->head].y = raw_y;
        q->events[q->head].type = type;
        q->head = next_head;
        q->tail = (q->tail + 1) % TOUCH_QUEUE_SIZE;
    }

    LeaveCriticalSection(&q->cs);
}

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
    init_screen_queues();
    if (buf) {
        memset(buf, 0, 128);
    }
    if (num_screens) {
        *num_screens = MAX_SCREENS; /* Report 2 screens for dual station */
    }
    log_info("EloGetScreenInfo called -> reporting %u screens", num_screens ? *num_screens : 0);
    return 0; /* 0 = EloSuccess */
}

static int __cdecl my_EloGetTouch(int *touch_data, int a2, int a3, unsigned int *screen_idx)
{
    if (!touch_data || !screen_idx) {
        return 1;
    }

    unsigned int screen = *screen_idx;
    if (screen >= MAX_SCREENS) {
        return 1;
    }

    init_screen_queues();
    struct elo_screen_queue *q = &s_screens[screen];

    EnterCriticalSection(&q->cs);
    if (q->tail != q->head) {
        struct elo_touch_event ev = q->events[q->tail];
        q->tail = (q->tail + 1) % TOUCH_QUEUE_SIZE;
        LeaveCriticalSection(&q->cs);

        touch_data[0] = ev.x;
        touch_data[1] = ev.y;
        touch_data[2] = 0;
        touch_data[3] = ev.type;

        static uint32_t s_log_touch = 0;
        if ((s_log_touch++ % 30) == 0 || ev.type == 1 || ev.type == 4) {
            log_info("EloGetTouch[screen %u]: x=%d, y=%d, type=%d (1=Down, 2=Move, 4=Up)",
                     screen, ev.x, ev.y, ev.type);
        }
        return 0; /* 0 = Data available */
    }

    LeaveCriticalSection(&q->cs);
    return 1; /* 1 = No touch */
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

static int __cdecl my_StartHooknMouse(void)
{
    log_info("StartHooknMouse() intercepted -> no-op to allow standard cursor");
    return 0;
}

static int __cdecl my_EndHooknMouse(void)
{
    log_info("EndHooknMouse() intercepted -> no-op");
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

static const struct hook_symbol kpm_hooknmouse_syms[] = {
    {
        .name = "StartHooknMouse",
        .patch = my_StartHooknMouse,
    },
    {
        .name = "EndHooknMouse",
        .patch = my_EndHooknMouse,
    },
};

void kpm_touch_hook_init(void)
{
    init_screen_queues();

    hook_table_apply(
        NULL,
        "EloPubIf.dll",
        kpm_elo_syms,
        lengthof(kpm_elo_syms));

    hook_table_apply(
        NULL,
        "libHooknMouse.dll",
        kpm_hooknmouse_syms,
        lengthof(kpm_hooknmouse_syms));

    log_info("EloPubIf & libHooknMouse touch panel hooks installed successfully");
}
