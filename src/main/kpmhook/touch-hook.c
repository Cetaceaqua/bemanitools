#define LOG_MODULE "kpm-touch"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hook/table.h"
#include "kpmhook/touch-hook.h"
#include "util/defs.h"
#include "util/log.h"

#define MAX_SCREENS 1
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
static volatile bool s_touch_activity = false;

bool kpm_touch_get_and_clear_activity(void)
{
    bool act = s_touch_activity;
    s_touch_activity = false;
    return act;
}

static void patch_memory(uintptr_t addr, const uint8_t *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
}

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
    /* KPM cabinet has only 1 touch screen on Station 0 (Main Screen) */
    if (screen != 0 || client_w <= 0 || client_h <= 0) {
        return;
    }
    s_touch_activity = true;
    init_screen_queues();

    /* Convert client pixels to Elo 12-bit ADC raw range (0..4092) with half-pixel rounding */
    int raw_x = (int) ((((int64_t) client_x * 4092) + (client_w / 2)) / client_w);
    int raw_y = (int) ((((int64_t) client_y * 4092) + (client_h / 2)) / client_h);

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

    /* If the last pending event in the queue is already a MOVE event and this is also a MOVE,
     * coalesce into it with the newest coordinates instead of bloating the FIFO queue */
    if (type == 2 && q->head != q->tail) {
        int last_idx = (q->head - 1 + TOUCH_QUEUE_SIZE) % TOUCH_QUEUE_SIZE;
        if (q->events[last_idx].type == 2) {
            q->events[last_idx].x = raw_x;
            q->events[last_idx].y = raw_y;
            LeaveCriticalSection(&q->cs);
            return;
        }
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
    log_misc("EloSetMouseMode(mode=%d, screen=%d)", mode, screen);
    return 0;
}

static int __cdecl my_EloSetTouchReportingState(int state, int screen)
{
    log_misc("EloSetTouchReportingState(state=%d, screen=%d)", state, screen);
    return 0;
}

static int __cdecl my_EloGetScreenInfo(void *buf, unsigned int *num_screens)
{
    init_screen_queues();
    if (buf) {
        memset(buf, 0, 128);
    }
    if (num_screens) {
        *num_screens = MAX_SCREENS; /* KPM cabinet has exactly 1 touch screen on Main Station 0 */
    }
    log_misc("EloGetScreenInfo called -> reporting %u screen(s)", num_screens ? *num_screens : 0);
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

        /* If consecutive MOVE events are queued up, skip directly to the latest one */
        while (ev.type == 2 && q->tail != q->head && q->events[q->tail].type == 2) {
            ev = q->events[q->tail];
            q->tail = (q->tail + 1) % TOUCH_QUEUE_SIZE;
        }

        LeaveCriticalSection(&q->cs);

        touch_data[0] = ev.x;
        touch_data[1] = ev.y;
        touch_data[2] = 0;
        touch_data[3] = ev.type;

        log_misc("EloGetTouch[screen %u]: x=%d, y=%d, type=%d", screen, ev.x, ev.y, ev.type);
        return 0; /* 0 = Data available */
    }

    LeaveCriticalSection(&q->cs);
    return 1; /* 1 = No touch */
}

static int __cdecl my_EloCancel(void)
{
    log_misc("EloCancel()");
    return 0;
}

static int __cdecl my_EloSetBeep(void *data, int screen)
{
    log_misc("EloSetBeep(screen=%d)", screen);
    return 0;
}

static int __cdecl my_StartHooknMouse(void)
{
    log_misc("StartHooknMouse() intercepted -> no-op to allow standard cursor");
    return 0;
}

static int __cdecl my_EndHooknMouse(void)
{
    log_misc("EndHooknMouse() intercepted -> no-op");
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

/*
 * Safe replacement for game's touch collision detection routines:
 * - 0x004197B0: SetMouseJustPush
 * - 0x004198B0: SetMouseJustUnPush
 * - 0x004199A0: SetMousePushing
 *
 * In the original game, these routines iterate over dword_E5F00C[0..10] up to 100 rects.
 * When switching scenes (e.g. entering Date Mode), stale/freed heap pointers left in
 * dword_E5F00C from transient objects (e.g. SB_MODE_SEL, roulette) cause an Access Violation (0xC0000005)
 * because they cross page boundaries when scanning for the -1 terminator.
 * These safe handlers use SEH to catch any invalid/dangling pointers, safely clear the slot,
 * and prevent game crashes.
 */

static void hook_func_jmp(uintptr_t target_site, const void *hook_fn)
{
    uint32_t rel = (uint32_t) ((uintptr_t) hook_fn - (target_site + 5));
    uint8_t jmp_bytes[5];
    jmp_bytes[0] = 0xE9;
    memcpy(&jmp_bytes[1], &rel, sizeof(rel));
    patch_memory(target_site, jmp_bytes, sizeof(jmp_bytes));
}

static int __cdecl my_SetMousePushing(int16_t a1, int16_t a2)
{
    int16_t **tables = (int16_t **) 0x00E5F00C;
    uint8_t *sw_data = (uint8_t *) 0x00E5F038;

    for (int t = 0; t < 11; t++) {
        int16_t *rects = tables[t];
        if (!rects) {
            continue;
        }

        __try {
            if (*rects < 0) {
                continue;
            }

            for (int i = 0; i < 100; i++) {
                int16_t rx = rects[4 * i];
                if (rx < 0) {
                    break;
                }
                int16_t ry = rects[4 * i + 1];
                int16_t rw = rects[4 * i + 2];
                int16_t rh = rects[4 * i + 3];

                if (a1 >= rx && a1 < rx + rw && a2 >= ry && a2 < ry + rh) {
                    sw_data[t * 100 + i] |= 2;
                } else {
                    sw_data[t * 100 + i] &= ~2;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log_warning("kpm-touch: caught invalid touch table[%d]=%p in SetMousePushing, auto-clearing slot", t, rects);
            tables[t] = NULL;
        }
    }
    return 1;
}

static int __cdecl my_SetMouseJustPush(int16_t a1, int16_t a2)
{
    int16_t **tables = (int16_t **) 0x00E5F00C;
    uint8_t *sw_data = (uint8_t *) 0x00E5F038;
    int8_t *recent = (int8_t *) 0x00E5EF06;

    for (int t = 0; t < 11; t++) {
        int16_t *rects = tables[t];
        if (!rects) {
            continue;
        }

        __try {
            if (*rects < 0) {
                continue;
            }

            for (int i = 0; i < 100; i++) {
                int16_t rx = rects[4 * i];
                if (rx < 0) {
                    break;
                }
                int16_t ry = rects[4 * i + 1];
                int16_t rw = rects[4 * i + 2];
                int16_t rh = rects[4 * i + 3];

                if (a1 >= rx && a1 < rx + rw && a2 >= ry && a2 < ry + rh) {
                    if ((sw_data[t * 100 + i] & 2) == 0) {
                        sw_data[t * 100 + i] = 3;
                        *recent = (int8_t) i;
                    }
                } else {
                    sw_data[t * 100 + i] &= ~2;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log_warning("kpm-touch: caught invalid touch table[%d]=%p in SetMouseJustPush, auto-clearing slot", t, rects);
            tables[t] = NULL;
        }
    }
    return 0;
}

static int __cdecl my_SetMouseJustUnPush(int16_t a1, int16_t a2)
{
    int16_t **tables = (int16_t **) 0x00E5F00C;
    uint8_t *sw_data = (uint8_t *) 0x00E5F038;

    for (int t = 0; t < 11; t++) {
        int16_t *rects = tables[t];
        if (!rects) {
            continue;
        }

        __try {
            if (*rects < 0) {
                continue;
            }

            for (int i = 0; i < 100; i++) {
                int16_t rx = rects[4 * i];
                if (rx < 0) {
                    break;
                }
                int16_t ry = rects[4 * i + 1];
                int16_t rw = rects[4 * i + 2];
                int16_t rh = rects[4 * i + 3];

                if (a1 >= rx && a1 < rx + rw && a2 >= ry && a2 < ry + rh) {
                    sw_data[t * 100 + i] = (sw_data[t * 100 + i] & 0xF9) | 4;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log_warning("kpm-touch: caught invalid touch table[%d]=%p in SetMouseJustUnPush, auto-clearing slot", t, rects);
            tables[t] = NULL;
        }
    }
    return 1;
}

static LONG WINAPI kpm_touch_veh_filter(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t ip = (uintptr_t) ep->ContextRecord->Eip;
        /* Check if crash occurred inside touch table scanning routines (0x004197B0 - 0x00419A90) */
        if (ip >= 0x004197B0 && ip < 0x00419A90) {
            log_warning("kpm-touch VEH: intercepted access violation in touch collision at 0x%p, recovering safely", (void *) ip);
            if (ip >= 0x004199A0) {
                ep->ContextRecord->Eip = 0x00419A8F;
            } else if (ip >= 0x004198B0) {
                ep->ContextRecord->Eip = 0x00419998;
            } else {
                ep->ContextRecord->Eip = 0x004198A7;
            }
            ep->ContextRecord->Eax = 1;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

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

    /* Patch 0x004187cf: JNZ short loc_418806 (75 35) -> NOP NOP (90 90)
     * Bypasses factory CRT bezel calibration distortion and forces 1:1 exact pixel mapping.
     */
    static const uint8_t nops_2[2] = { 0x90, 0x90 };
    patch_memory(0x004187cf, nops_2, sizeof(nops_2));
    log_info("Patched 0x004187cf: Bypassed Elo factory bezel calibration distortion (1:1 pixel mapping enabled)");

    /* Install safe SEH touch table collision handlers to prevent 0xC0000005 crash on scene transitions */
    hook_func_jmp(0x004197B0, my_SetMouseJustPush);
    hook_func_jmp(0x004198B0, my_SetMouseJustUnPush);
    hook_func_jmp(0x004199A0, my_SetMousePushing);
    log_info("Patched 0x004197B0, 0x004198B0, 0x004199A0: Safe SEH touch table collision handlers installed");

    /* Install Vectored Exception Handler as ultimate safety net for any touch collision edge cases */
    AddVectoredExceptionHandler(1, kpm_touch_veh_filter);
    log_info("Installed Vectored Exception Handler for touch collision safety");

    log_info("EloPubIf & libHooknMouse touch panel hooks installed successfully");
}
