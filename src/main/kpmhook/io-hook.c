#define LOG_MODULE "kpm-io-hook"

#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#pragma comment(lib, "winmm.lib")

#include "bemanitools/kpmio.h"
#include "kpmhook/config-io.h"
#include "kpmhook/io-hook.h"
#include "kpmhook/touch-hook.h"
#include "util/log.h"
#include "util/thread.h"

#define ADDR_KEYBOARD_LOOP_START  0x00411686
#define ADDR_KEYBOARD_LOOP_END    0x00411C56
#define ADDR_SUBBOARD_CHECK_JUMP  0x00411CC4
#define ADDR_DWORD_1ACFCB8        0x01ACFCB8
#define ADDR_DWORD_1ACFCBC        0x01ACFCBC
#define ADDR_CHECK_PCSUB_MODE_JUMP   0x0041ECF7
#define ADDR_NO_SUBBOARD_CHECK_CALL  0x0041F4DE
#define ADDR_COMMON_INIT_TUTORIAL_FLAG 0x0044AD75
#define ADDR_CHECK_EAMUSE_PLUG_JUMP  0x0041FDC6
#define ADDR_PCSUB_GET_NET_PLUG_ID   0x005CA870
#define ADDR_HOPPER_PAY_STOP_FUNC    0x005C93A0
#define ADDR_SECRET_MENU_COUNT_DEC   0x0042C199
#define ADDR_SECRET_MENU_DRAW_FILTER 0x0042C550
#define ADDR_ATTRACT_TIMEOUT_1       0x00473670
#define ADDR_ATTRACT_TIMEOUT_2       0x00473DA3
#define ADDR_ATTRACT_FUNDS_CHECK_1   0x00473685
#define ADDR_ATTRACT_FUNDS_CHECK_2   0x00473DB8
#define ADDR_ATTRACT_SKIP_JUMP       0x00473D90
#define ADDR_ATTRACT_CREDIT_BYPASS   0x00473DA9
#define ADDR_SLOT_IDLE_TIMER_RESET   0x00471362
#define ADDR_STEP39_ABORT_CHECK      0x0047BCE0
#define ADDR_BETSLOT_SKIP_JUMP       0x004C43FB
#define ADDR_BETSLOT_TIMEOUT         0x004C4412
#define ADDR_BETSLOT_FUNDS_CHECK     0x004C442B
#define ADDR_BETSLOT_CREDIT_BYPASS   0x004C4440
#define ADDR_COIN_DWELL_TIMEOUT_JUMP 0x00402079
#define ADDR_TITLE_SET_TUTORIAL_FLAG      0x004A270C
#define ADDR_PANEL_CHECK_TUTORIAL_FLAG    0x00473F2B
#define ADDR_BOOT_STATUS_INIT_MODE   0x00425A60
#define ADDR_STARTUP_MODE_SWITCH_JUMP 0x004263C1
#define ADDR_OS_VERSION_CHECK        0x00427050
#define ADDR_OS_VERSION_STRING       0x00E5EE10
#define ADDR_NVRAM_SRAM_BUFFER       0x01D02970
#define ADDR_NVRAM_ERROR_FLAGS       0x01ACE7E4
#define ADDR_STATION_CTX_0           0x00E652E0
#define ADDR_STATION_CTX_1           0x00E652E4
#define ADDR_STATION_CTX_2           0x00E652E8
#define ADDR_GLOBAL_EAMUSE_CONTROL   0x01ACEAB4
#define ADDR_EAMUSE_GET_STATUS_FUNC  0x005499B0
#define ADDR_EAMUSE_VFTABLE          0x00C3C3CC
#define ADDR_PCSUB_CHECK_DONGLES_FUNC 0x005DD310

#define ADDR_NVRAM_SUBBOARD_BRANCH   0x00504695
#define ADDR_NVRAM_MEMSET_CALL       0x005046A7
#define NVRAM_SRAM_SIZE              0x00080000 /* 512 KB battery-backed SRAM */
#define ADDR_FUNC_SUB_4532A0         0x004532A0
#define ADDR_FUNC_SUB_453280         0x00453280

#define ADDR_MAINAPP_UPDATE_GATE_1        0x00403563
#define ADDR_MAINAPP_UPDATE_GATE_2        0x0040356C
#define ADDR_TRANSFER_CREDIT_LIMIT_JUMP   0x00401C7B
#define ADDR_COLLECT_STATE_GATE_JUMP      0x00402233
#define ADDR_COLLECT_DEBOUNCE_CMP         0x004022FB
#define ADDR_COLLECT_NO_HOPPER_JUMP       0x004022BC
#define ADDR_COLLECT_OVERFLOW_JUMP        0x00402321
#define ADDR_COLLECT_COMPLETE_ERR_JUMP    0x004029C8
#define ADDR_PAYOUT_EMERGENCY_STOP_1      0x004024EB
#define ADDR_PAYOUT_EMERGENCY_STOP_2      0x00402535
#define ADDR_PAYOUT_CASE2_STEP_JUMP       0x00402802
#define ADDR_KPM_ERROR_DISPATCHER         0x004232D0

#define TOTAL_LEDS                110
#define NUM_STRIPS                11
#define LEDS_PER_STRIP            10

/* Strip layout:
 * Strips 0..2  (LEDs 0..29):   SCREEN LED 01..03
 * Strips 3..4  (LEDs 30..49):  FRONT LED LEFT, RIGHT
 * Strips 5..7  (LEDs 50..79):  SIDE LED LEFT 01..03
 * Strips 8..10 (LEDs 80..109): SIDE LED RIGHT 01..03
 */

static bool s_io_initialized = false;
static struct kpmhook_config_io s_cfg;
static HANDLE s_light_serial = INVALID_HANDLE_VALUE;
static uint8_t s_cabinet_jumper_byte = 0x02; /* Default Manaka: bit 1 (0x02) */

static void patch_memory(uintptr_t addr, const uint8_t *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
}

static void patch_call(uintptr_t call_site, const void *target_func)
{
    uint32_t rel = (uint32_t) ((uintptr_t) target_func - (call_site + 5));
    uint8_t call_bytes[5];
    call_bytes[0] = 0xE8;
    memcpy(&call_bytes[1], &rel, sizeof(rel));
    patch_memory(call_site, call_bytes, sizeof(call_bytes));
}

static void patch_jmp(uintptr_t jmp_site, const void *target_func)
{
    uint32_t rel = (uint32_t) ((uintptr_t) target_func - (jmp_site + 5));
    uint8_t jmp_bytes[5];
    jmp_bytes[0] = 0xE9;
    memcpy(&jmp_bytes[1], &rel, sizeof(rel));
    patch_memory(jmp_site, jmp_bytes, sizeof(jmp_bytes));
}

static int __stdcall my_os_version_check(int a1)
{
    strncpy((char *) ADDR_OS_VERSION_STRING, "KONAMI OS 2012", 31);
    ((char *) ADDR_OS_VERSION_STRING)[31] = '\0';
    return 1; /* 1 = OK */
}

static bool s_nvram_initialized = false;
static uint32_t s_last_nvram_hash = 0;
static DWORD s_last_nvram_sync_tick = 0;

static uint32_t fnv1a_32(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void get_nvram_file_path(char *buf, size_t max_len)
{
    CreateDirectoryA("dev", NULL);
    CreateDirectoryA("dev\\NVRAM", NULL);
    snprintf(buf, max_len, "dev\\NVRAM\\sram.bin");
}

static void *kpm_nvram_init(void *dest, int c, size_t count)
{
    char path[MAX_PATH];
    get_nvram_file_path(path, sizeof(path));

    log_info("Initializing 512 KB battery-backed SRAM NVRAM emulation...");
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Check if legacy NVRAM\sram.bin exists and migrate it */
        FILE *f_old = fopen("NVRAM\\sram.bin", "rb");
        if (f_old) {
            f = f_old;
            log_info("Migrating legacy NVRAM\\sram.bin to %s", path);
        }
    }
    if (f) {
        size_t n = fread(dest, 1, count, f);
        fclose(f);
        if (n == count) {
            s_last_nvram_hash = fnv1a_32((const uint8_t *) dest, count);
            s_nvram_initialized = true;
            s_last_nvram_sync_tick = GetTickCount();
            log_info(
                "Successfully loaded 512 KB NVRAM from %s (hash: 0x%08X)",
                path,
                s_last_nvram_hash);
            return dest;
        }
        log_warning("NVRAM file %s size mismatch (%zu / %zu). Re-initializing blank SRAM.", path, n, count);
    } else {
        log_info("No existing NVRAM file found at %s. Initializing blank SRAM buffer...", path);
    }

    memset(dest, c, count);
    s_last_nvram_hash = fnv1a_32((const uint8_t *) dest, count);
    s_nvram_initialized = true;
    s_last_nvram_sync_tick = GetTickCount();
    return dest;
}

static void kpm_nvram_flush(void)
{
    if (!s_nvram_initialized) {
        return;
    }

    const uint8_t *sram = (const uint8_t *) ADDR_NVRAM_SRAM_BUFFER;
    uint32_t cur_hash = fnv1a_32(sram, NVRAM_SRAM_SIZE);
    if (cur_hash == s_last_nvram_hash) {
        return;
    }

    char path[MAX_PATH];
    char tmp_path[MAX_PATH];
    get_nvram_file_path(path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        log_warning("Failed to open temp NVRAM file for writing: %s", tmp_path);
        return;
    }

    size_t written = fwrite(sram, 1, NVRAM_SRAM_SIZE, f);
    fflush(f);
    fclose(f);

    if (written == NVRAM_SRAM_SIZE) {
        if (MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING)) {
            s_last_nvram_hash = cur_hash;
            log_info("NVRAM saved to %s (hash: 0x%08X)", path, cur_hash);
        } else {
            log_warning("Failed to atomically replace NVRAM file %s (error %lu)", path, GetLastError());
        }
    } else {
        log_warning("Failed to write complete NVRAM: wrote %zu / %d", written, NVRAM_SRAM_SIZE);
    }
}

void kpm_io_hook_flush_nvram(void)
{
    kpm_nvram_flush();
}

/*
 * sub_41F410 CheckInitError calls KPMConfig_GetInt(this, "NO_SUBBOARD", 0) at 0x0041F4DE.
 * Original signature is __thiscall int KPMConfig_GetInt(void *this, const char *key, int default_val).
 * In MSVC x86, __fastcall puts `this` in ECX and `edx_unused` in EDX, matching __thiscall register conventions.
 */
static int __fastcall my_check_no_subboard(void *this_ptr, void *edx_unused, const char *key, int default_val)
{
    uint32_t *p_nvram_error = (uint32_t *) ADDR_NVRAM_ERROR_FLAGS;
    if (p_nvram_error && *p_nvram_error == 0) {
        log_info(
            "NVRAM integrity verified (0x01ACE7E4 == 0), bypassing NO_SUBBOARD wipe at 0x%08X",
            ADDR_NO_SUBBOARD_CHECK_CALL);
        return 0; /* Returns 0 so `cmp eax, ebx; jz loc_41F5EE` cleanly skips erasing partitions */
    }

    log_info(
        "NVRAM error flags non-zero (0x%08X), allowing initial default SRAM partition format",
        p_nvram_error ? *p_nvram_error : 0xFFFFFFFF);
    return 1;
}

#define ADDR_CHUMANINPUTDEVICE_VTABLE_SLOT5 0x00BBC180

static char (__fastcall *real_GetButtonState)(void *this_ptr, void *edx_unused, int channel) = NULL;

static DWORD s_transfer_press_start_tick = 0;

/*
 * CHumanInputDevice::vftable[5] (GetButtonState, 0x00411F00):
 * In MSVC x86, __thiscall passes `this` in ECX, `channel` at [esp+4].
 * __fastcall with an unused second parameter matches this calling convention exactly.
 */
static char __fastcall my_GetButtonState(void *this_ptr, void *edx_unused, int channel)
{
    if (channel == 9) {
        uint16_t btns = kpm_io_get_buttons();
        bool transfer_active = (btns & KPM_IO_BTN_TRANSFER) != 0;

        if (transfer_active) {
            DWORD now = GetTickCount();
            if (s_transfer_press_start_tick == 0) {
                s_transfer_press_start_tick = now;
                return 1;
            }
            DWORD elapsed = now - s_transfer_press_start_tick;
            /* Initial hold delay: 300ms before auto-repeat kicks in */
            if (elapsed < 300) {
                return 1;
            }
            /* Auto-repeat phase: 120ms period (~8 coins/sec)
             * [0..39ms]: 0 (Release pulse, ~2.4 frames, clears latch [lpCriticalSectionr+276] at 0x401df8)
             * [40..119ms]: 1 (Press pulse, ~4.8 frames >= 2 debounce, triggers sub_401930 coin transfer)
             */
            DWORD cycle = (elapsed - 300) % 120;
            return (cycle >= 40) ? 1 : 0;
        } else {
            s_transfer_press_start_tick = 0;
            return 0;
        }
    } else if (channel == 1) {
        uint16_t btns = kpm_io_get_buttons();
        if (btns & KPM_IO_BTN_COLLECT_PAYOUT) {
            return 1;
        }
        return 0;
    } else if (channel == 6) {
        uint16_t btns = kpm_io_get_buttons();
        if (btns & KPM_IO_BTN_RESET_KEY) {
            return 1;
        }
        return 0;
    }
    if (real_GetButtonState) {
        return real_GetButtonState(this_ptr, edx_unused, channel);
    }
    return 0;
}

static void init_light_serial(const struct kpmhook_config_io *cfg)
{
    if (!cfg->light_port[0]) {
        return;
    }

    char port_path[64];
    snprintf(port_path, sizeof(port_path), "\\\\.\\%s", cfg->light_port);

    s_light_serial = CreateFileA(
        port_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (s_light_serial == INVALID_HANDLE_VALUE) {
        log_warning(
            "Could not open illumination serial port '%s' (Error %lu)",
            cfg->light_port,
            GetLastError());
        return;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(s_light_serial, &dcb)) {
        dcb.BaudRate = cfg->light_baud > 0 ? cfg->light_baud : CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(s_light_serial, &dcb);
    }

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 10;
    SetCommTimeouts(s_light_serial, &timeouts);

    log_info(
        "Cabinet illumination serial pass-through active on %s at %d baud",
        cfg->light_port,
        cfg->light_baud);
}

static void reset_station_idle_timers(uint8_t *st, DWORD now)
{
    if (!st) return;
    /* Station::GetGameStatus() is stored directly at st + 0x58 (sub_42A540) */
    uint8_t *status = *((uint8_t **) (st + 0x58));
    if (!status) return;

    /* 1. Mode 5 (CGameNormalMode) idle timer at status + 0x247C + 0x70A78.
     * This watchdog ticks and triggers Step 39 exit to title if idle. */
    uint32_t *p_slot_timer = (uint32_t *) (status + 0x247C + 0x70A78);
    *p_slot_timer = now;

    /* 2. Mode 11 (CGameBetSlot) idle timer */
    uint8_t *betslot = status + 0x76F48;
    uint32_t *p_slotmain = (uint32_t *) (betslot + 116);
    if (p_slotmain && *p_slotmain) {
        uint32_t *p_bet_timer = (uint32_t *) (*p_slotmain + 360);
        *p_bet_timer = now;
    }

    /* 3. Mode 0 (CGameTitle) idle timer at status + 0xD8 + 0x50 (thisa[20]) */
    uint32_t *p_title_timer = (uint32_t *) (status + 0xD8 + 0x50);
    *p_title_timer = now;
}

static void reset_game_idle_timers(void)
{
    uint32_t *p_app = (uint32_t *) 0x01ACE78C;
    if (!p_app || !*p_app) return;
    uint8_t *app = (uint8_t *) (*p_app);
    DWORD now = timeGetTime();

    /* Station 0 at app + 0x8C */
    uint32_t *p_st0 = (uint32_t *) (app + 0x8C);
    if (p_st0 && *p_st0) {
        reset_station_idle_timers((uint8_t *) (*p_st0), now);
    }

    /* Station 1 at app + 0x90 */
    uint32_t *p_st1 = (uint32_t *) (app + 0x90);
    if (p_st1 && *p_st1) {
        reset_station_idle_timers((uint8_t *) (*p_st1), now);
    }
}

/*
 * Coin asset protection hooks:
 * Original logic in Mode 5 (0x00473685, 0x00473DB8) and Mode 11 (0x004C442B)
 * only compares Credit ([eax+0x2578]) against 0 when idle timer reaches timeout threshold.
 * If Credit == 0 (even if Coin pool [eax+0x2588] has un-transferred coins), it triggers Step 39
 * and forces an abrupt transition back to Title/Attract loop.
 * These hooks evaluate (Credit | Coin) so any un-transferred coins protect against timeout exit.
 */
static const uintptr_t CONT_CHECK_FUNDS_1 = 0x00473690;
static __declspec(naked) void hook_check_funds_1(void)
{
    __asm {
        mov eax, dword ptr ds:[0x00E652D4]
        mov edi, [eax+0x2578]   ; Credit
        or edi, [eax+0x2588]    ; Coin
        jmp dword ptr [CONT_CHECK_FUNDS_1]
    }
}

static const uintptr_t CONT_CHECK_FUNDS_2 = 0x00473DC4;
static __declspec(naked) void hook_check_funds_2(void)
{
    __asm {
        mov ecx, dword ptr ds:[0x00E652D4]
        mov edx, [ecx+0x2578]   ; Credit
        or edx, [ecx+0x2588]    ; Coin
        jmp dword ptr [CONT_CHECK_FUNDS_2]
    }
}

static const uintptr_t CONT_CHECK_FUNDS_3 = 0x004C4437;
static __declspec(naked) void hook_check_funds_3(void)
{
    __asm {
        mov edx, dword ptr ds:[0x00E652D4]
        mov edi, [edx+0x2578]   ; Credit
        or edi, [edx+0x2588]    ; Coin
        jmp dword ptr [CONT_CHECK_FUNDS_3]
    }
}

static const uintptr_t SUB_5C93A0_CONT = 0x005C93AB;

static void my_sub_5C93A0_impl(void)
{
    log_info("sub_5C93A0 intercepted (pay stop ignored to protect active virtual hopper payout)");
}

static void __cdecl my_sub_4232D0(int a1, unsigned int code, int a3, int a4)
{
    log_warning(
        "KPM Fatal Error suppressed by io-hook: code=0x%04X, a1=%d, a3=%d, a4=%d",
        code, a1, a3, a4);
}

static __declspec(naked) void my_sub_5C93A0(void)
{
    __asm {
        pushad
        call my_sub_5C93A0_impl
        popad

        push ebp
        mov ebp, esp
        and esp, 0xFFFFFFF8
        mov eax, dword ptr ds:[0x01ACFCB8]
        jmp dword ptr [SUB_5C93A0_CONT]
    }
}

static const char S_KONAMI_PCB_ID[] = "014014000003CCCBC50D";

/* Standard 40-byte DS2430A Dongle structure (32B Payload + 8B ROM ID) */
struct ds2430a_dongle {
    uint8_t rom_id[8];
    uint8_t payload[32];
};

/* Default authenticated DS2430A White Network Plug (E-AMUSE3, @@@@@@@@, PCBID: 014014000003CCCBC50D) */
static const uint8_t S_DEFAULT_WHITE_ROM_ID[8] = {
    0x14, 0x00, 0x00, 0x03, 0xCC, 0xCB, 0xC5, 0x0D
};
static const uint8_t S_DEFAULT_WHITE_PAYLOAD[32] = {
    0x6A, 0xB1, 0x25, 0xA2, 0xD1, 0xB1, 0x20, 0x08,
    0x82, 0x20, 0x08, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x56
};

/* Default authenticated DS2430A Black Software Plug (LOUSSTAI, GSKPMJAA) */
static const uint8_t S_DEFAULT_BLACK_ROM_ID[8] = {
    0x14, 0x20, 0x03, 0x58, 0x01, 0x00, 0x00, 0x98
};
static const uint8_t S_DEFAULT_BLACK_PAYLOAD[32] = {
    0xC9, 0x5D, 0x85, 0xF0, 0xD5, 0xA4, 0xE7, 0xBC,
    0xC2, 0xAD, 0x1A, 0x86, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA
};

static struct ds2430a_dongle s_net_dongle;
static struct ds2430a_dongle s_soft_dongle;
static bool s_dongles_initialized = false;

static void load_ds2430a_dongle(
    const char *primary_filename,
    const char *secondary_filename,
    struct ds2430a_dongle *out_dongle,
    const uint8_t *default_rom_id,
    const uint8_t *default_payload)
{
    FILE *f = fopen(primary_filename, "rb");
    if (!f && secondary_filename) {
        f = fopen(secondary_filename, "rb");
    }

    if (f) {
        uint8_t buf[40];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n == 40) {
            /* Check layout format:
             * MAME layout: Payload[0..31] + ROM_ID[32..39] (Family Code 0x14 at buf[32])
             * Legacy layout: ROM_ID[0..7] + Payload[8..39] (Family Code 0x14 at buf[0])
             */
            if (buf[32] == 0x14) {
                memcpy(out_dongle->payload, &buf[0], 32);
                memcpy(out_dongle->rom_id, &buf[32], 8);
                log_info(
                    "Loaded DS2430A dongle from %s (MAME layout, ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X)",
                    primary_filename,
                    out_dongle->rom_id[0], out_dongle->rom_id[1], out_dongle->rom_id[2], out_dongle->rom_id[3],
                    out_dongle->rom_id[4], out_dongle->rom_id[5], out_dongle->rom_id[6], out_dongle->rom_id[7]);
                return;
            } else if (buf[0] == 0x14) {
                memcpy(out_dongle->rom_id, &buf[0], 8);
                memcpy(out_dongle->payload, &buf[8], 32);
                log_info(
                    "Loaded DS2430A dongle from %s (Legacy layout, ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X)",
                    primary_filename,
                    out_dongle->rom_id[0], out_dongle->rom_id[1], out_dongle->rom_id[2], out_dongle->rom_id[3],
                    out_dongle->rom_id[4], out_dongle->rom_id[5], out_dongle->rom_id[6], out_dongle->rom_id[7]);
                return;
            }
        }
    }

    /* Fallback to default verified parameters */
    memcpy(out_dongle->rom_id, default_rom_id, 8);
    memcpy(out_dongle->payload, default_payload, 32);
    log_info(
        "Using built-in authenticated DS2430A dongle for %s (ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X)",
        primary_filename,
        out_dongle->rom_id[0], out_dongle->rom_id[1], out_dongle->rom_id[2], out_dongle->rom_id[3],
        out_dongle->rom_id[4], out_dongle->rom_id[5], out_dongle->rom_id[6], out_dongle->rom_id[7]);
}

static void init_ds2430a_dongles(void)
{
    if (s_dongles_initialized) {
        return;
    }
    s_dongles_initialized = true;

    load_ds2430a_dongle(
        "dongle_network.bin",
        "contents/dongle_network.bin",
        &s_net_dongle,
        S_DEFAULT_WHITE_ROM_ID,
        S_DEFAULT_WHITE_PAYLOAD);

    load_ds2430a_dongle(
        "dongle_software.bin",
        "contents/dongle_software.bin",
        &s_soft_dongle,
        S_DEFAULT_BLACK_ROM_ID,
        S_DEFAULT_BLACK_PAYLOAD);
}


/*
 * sub_5CA870 CPcSubWrap::GetNetPlugPcbId(char *buf, unsigned int max_len)
 * __thiscall calling convention (ECX = this, [esp+4] = buf, [esp+8] = max_len).
 * Returns `buf` on success, pops 8 bytes of stack arguments upon return.
 */
static int __fastcall my_sub_5CA870(void *this_ptr, void *edx_unused, char *buf, unsigned int max_len)
{
    if (buf && max_len > 0) {
        size_t id_len = strlen(S_KONAMI_PCB_ID);
        if (id_len >= max_len) {
            id_len = max_len - 1;
        }
        memcpy(buf, S_KONAMI_PCB_ID, id_len);
        buf[id_len] = '\0';
    }
    return (int) (uintptr_t) buf;
}

void kpm_io_hook_init(const struct kpmhook_config_io *cfg)
{
    log_info("Initializing PCSub arcade I/O virtualization...");
    s_cfg = *cfg;

    init_ds2430a_dongles();

    /* Hook sub_5C93A0 (Hopper Pay Stop / Command 285) */
    uint8_t jmp_stop[11] = {
        0xE9, 0x00, 0x00, 0x00, 0x00, /* jmp rel32 */
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90 /* 6 NOPs */
    };
    uint32_t rel_stop = (uint32_t) my_sub_5C93A0 - (ADDR_HOPPER_PAY_STOP_FUNC + 5);
    memcpy(&jmp_stop[1], &rel_stop, sizeof(rel_stop));
    patch_memory(ADDR_HOPPER_PAY_STOP_FUNC, jmp_stop, sizeof(jmp_stop));
    log_info("Hooked sub_5C93A0 at 0x%08X to intercept hopper pay stop", ADDR_HOPPER_PAY_STOP_FUNC);

    /* Hook sub_5CA870 (CPcSubWrap::GetNetPlugPcbId) to supply standard Konami PCB ID */
    uint8_t jmp_sub_5CA870[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    uint32_t rel_plug = (uint32_t) my_sub_5CA870 - (ADDR_PCSUB_GET_NET_PLUG_ID + 5);
    memcpy(&jmp_sub_5CA870[1], &rel_plug, sizeof(rel_plug));
    patch_memory(ADDR_PCSUB_GET_NET_PLUG_ID, jmp_sub_5CA870, sizeof(jmp_sub_5CA870));
    log_info(
        "Hooked sub_5CA870 at 0x%08X to return authentic PCB ID (%s)",
        ADDR_PCSUB_GET_NET_PLUG_ID,
        S_KONAMI_PCB_ID);

    /* Patch 0x0041FDC6 in CBootStatus::CheckEamuse:
     * Originally: cmp eax, edi; jnz loc_41FE61 (0F 85 95 00 00 00)
     * Replace with: jmp loc_41FE61; nop (E9 96 00 00 00 90)
     * Ensures CheckEamuse unconditionally advances through dummy board / eamuse init path
     * without blocking even if physical DS2432 hardware network plug is absent.
     */
    static const uint8_t jmp_eamuse_bypass[6] = {
        0xE9, 0x96, 0x00, 0x00, 0x00, /* jmp loc_41FE61 */
        0x90                          /* nop */
    };
    patch_memory(ADDR_CHECK_EAMUSE_PLUG_JUMP, jmp_eamuse_bypass, sizeof(jmp_eamuse_bypass));
    log_info(
        "Patched CheckEamuse security plug jump at 0x%08X to unconditional bypass",
        ADDR_CHECK_EAMUSE_PLUG_JUMP);

    /* Note: kpm_io_init is deferred to kpm_io_hook_update() on the first frame
     * to avoid Windows Loader Lock deadlock during DllMain process attach. */

    if (cfg->disable_debug_keys) {
        static const uint8_t jmp_skip_debug[6] = {
            0xE9, 0xCB, 0x05, 0x00, 0x00, /* jmp 0x00411C56 */
            0x90                          /* nop */
        };
        patch_memory(ADDR_KEYBOARD_LOOP_START, jmp_skip_debug, sizeof(jmp_skip_debug));
        log_info(
            "Disabled built-in developer keyboard debug controls (0x%08X -> 0x%08X)",
            ADDR_KEYBOARD_LOOP_START,
            ADDR_KEYBOARD_LOOP_END);
    } else {
        log_info("Built-in developer keyboard debug controls are ENABLED");
    }

    static const uint8_t nops6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    patch_memory(ADDR_SUBBOARD_CHECK_JUMP, nops6, sizeof(nops6));
    log_info("Patched subboard check jump at 0x%08X to NOPs", ADDR_SUBBOARD_CHECK_JUMP);

    /* Patch 0x0041ECF7: In CBootStatus::CheckPCSUB (0x0041EBC0), force step to 700.
     * Originally: neg eax; sbb eax, eax; and eax, 258h (9 bytes: F7 D8 19 C0 25 58 02 00 00)
     * We replace with: mov eax, 258h; nop; nop; nop; nop (B8 58 02 00 00 90 90 90 90)
     * Followed by original: add eax, 64h -> eax becomes 700 (2BCh).
     * This ensures CheckPCSUB immediately advances to step 700 regardless of NO_SUBBOARD=0 or 1,
     * completely bypassing the missing PLX 9030 PCI hardware test (error 0xF7D0).
     */
    static const uint8_t force_step_700[9] = {
        0xB8, 0x58, 0x02, 0x00, 0x00, /* mov eax, 258h */
        0x90, 0x90, 0x90, 0x90        /* 4 x NOP */
    };
    patch_memory(ADDR_CHECK_PCSUB_MODE_JUMP, force_step_700, sizeof(force_step_700));
    log_info(
        "Patched CheckPCSUB step at 0x%08X to force step 700 (NO_SUBBOARD=0 bypass active)",
        ADDR_CHECK_PCSUB_MODE_JUMP);



    /* Hook sub_41F410 CheckInitError NO_SUBBOARD check to protect SRAM settings */
    patch_call(ADDR_NO_SUBBOARD_CHECK_CALL, my_check_no_subboard);
    log_info(
        "Hooked NO_SUBBOARD initialization check at 0x%08X to protect SRAM persistence",
        ADDR_NO_SUBBOARD_CHECK_CALL);

    /* Hook CHumanInputDevice::vftable[5] (GetButtonState at 0x00BBC180) to provide direct,
     * debounced Transfer (Channel 9) button input straight to game logic */
    uintptr_t vtable_slot5 = ADDR_CHUMANINPUTDEVICE_VTABLE_SLOT5;
    real_GetButtonState = (char (__fastcall *)(void *, void *, int)) (*((uint32_t *) vtable_slot5));
    void *hook_get_btn = (void *) my_GetButtonState;
    patch_memory(vtable_slot5, (const uint8_t *) &hook_get_btn, sizeof(hook_get_btn));
    log_info(
        "Hooked CHumanInputDevice::GetButtonState at vtable 0x%08X (original: 0x%08X)",
        vtable_slot5,
        (uint32_t) real_GetButtonState);

    if (cfg->show_secret_menu) {
        /* In CTestModeMenuCustom::Init (0x0042C180):
         * 0x0042C199: add dword ptr [esi+94h], 0FFFFFFFFh (7 bytes: 83 86 94 00 00 00 FF)
         * NOP this out so m_nItems is not decremented from 6 to 5.
         * This allows the cursor to move down to index 4 (SECRET MODE).
         */
        static const uint8_t nops7[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        patch_memory(ADDR_SECRET_MENU_COUNT_DEC, nops7, sizeof(nops7));

        /* In CTestModeMenuCustom::DrawItem (0x0042C3C0):
         * 0x0042C550 - 0x0042C567 (23 bytes):
         * Originally checks if the item is ".." (GAME MODE). If not, it skips drawing.
         * We replace it with checking whether the item index <= 4 (which draws both
         * GAME MODE [index 3] and SECRET MODE [index 4], but skips CALIBRATION MODE [index 5]).
         *
         * 8B 44 24 20          mov eax, [esp+20h]      ; eax = &currentIndex
         * 83 38 04             cmp dword ptr [eax], 4  ; currentIndex <= 4 ?
         * 77 4C                ja  0x0042C5A5          ; if > 4, skip drawing!
         * 90 ... (14 bytes)    nop
         */
        static const uint8_t patch_draw[23] = {
            0x8B, 0x44, 0x24, 0x20,
            0x83, 0x38, 0x04,
            0x77, 0x4C,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        };
        patch_memory(ADDR_SECRET_MENU_DRAW_FILTER, patch_draw, sizeof(patch_draw));
        log_info(
            "Unlocked and enabled SECRET MODE directly in arcade Test Menu (0x%08X, 0x%08X)",
            ADDR_SECRET_MENU_COUNT_DEC,
            ADDR_SECRET_MENU_DRAW_FILTER);
    }

    if (cfg->attract_timeout > 0) {
        uint32_t timeout_ms = (uint32_t) cfg->attract_timeout * 1000;
        patch_memory(ADDR_ATTRACT_TIMEOUT_1, (const uint8_t *) &timeout_ms, sizeof(timeout_ms));
        patch_memory(ADDR_ATTRACT_TIMEOUT_2, (const uint8_t *) &timeout_ms, sizeof(timeout_ms));
        log_info(
            "Patched Attract idle timeout to %d seconds (%u ms) at 0x%08X and 0x%08X",
            cfg->attract_timeout,
            timeout_ms,
            ADDR_ATTRACT_TIMEOUT_1,
            ADDR_ATTRACT_TIMEOUT_2);

        /* sub_471320 (Mode 5 per-frame dispatcher) overwrites the idle timer [this+0x70A78]
         * with timeGetTime() every frame (89 86 78 0A 07 00), so the elapsed idle time comparison
         * can never reach the timeout threshold. NOP this out so the timer ticks legitimately
         * from Substate 3->4 transition (0x0047313F) and post-timeout re-arm (0x00473DE4). */
        static const uint8_t nops6_timer[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        patch_memory(ADDR_SLOT_IDLE_TIMER_RESET, nops6_timer, sizeof(nops6_timer));
        log_info(
            "Patched per-frame slot idle timer overwrite at 0x%08X to NOPs",
            ADDR_SLOT_IDLE_TIMER_RESET);

        /* Mode 11 (CGameSlotMainBet) idle timeout threshold */
        patch_memory(ADDR_BETSLOT_TIMEOUT, (const uint8_t *) &timeout_ms, sizeof(timeout_ms));
        log_info(
            "Configured Mode 11 (BetSlot) idle timeout threshold (%u ms at 0x%08X), authentic credit protection preserved",
            timeout_ms,
            ADDR_BETSLOT_TIMEOUT);
    }

    /* Install coin asset protection hooks for Mode 5 (0x00473685, 0x00473DB8) and Mode 11 (0x004C442B):
     * Original logic only checks Credit [eax+0x2578] and forces Step 39 exit to Title if Credit == 0.
     * By OR-ing with Coin pool [eax+0x2588], any un-transferred coins protect against timeout exit. */
    uint8_t jmp_funds_1[11] = {
        0xE9, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    uint32_t rel_f1 = (uint32_t) hook_check_funds_1 - (ADDR_ATTRACT_FUNDS_CHECK_1 + 5);
    memcpy(&jmp_funds_1[1], &rel_f1, sizeof(rel_f1));
    patch_memory(ADDR_ATTRACT_FUNDS_CHECK_1, jmp_funds_1, sizeof(jmp_funds_1));

    uint8_t jmp_funds_2[12] = {
        0xE9, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    uint32_t rel_f2 = (uint32_t) hook_check_funds_2 - (ADDR_ATTRACT_FUNDS_CHECK_2 + 5);
    memcpy(&jmp_funds_2[1], &rel_f2, sizeof(rel_f2));
    patch_memory(ADDR_ATTRACT_FUNDS_CHECK_2, jmp_funds_2, sizeof(jmp_funds_2));

    uint8_t jmp_funds_3[12] = {
        0xE9, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    uint32_t rel_f3 = (uint32_t) hook_check_funds_3 - (ADDR_BETSLOT_FUNDS_CHECK + 5);
    memcpy(&jmp_funds_3[1], &rel_f3, sizeof(rel_f3));
    patch_memory(ADDR_BETSLOT_FUNDS_CHECK, jmp_funds_3, sizeof(jmp_funds_3));

    log_info(
        "Installed coin asset protection hooks at 0x%08X, 0x%08X, and 0x%08X",
        ADDR_ATTRACT_FUNDS_CHECK_1,
        ADDR_ATTRACT_FUNDS_CHECK_2,
        ADDR_BETSLOT_FUNDS_CHECK);

    /* Patch 0x00402079: In sub_401930, disable 30-second un-transferred coin auto-payout.
     * Originally: 0F 86 2B 01 00 00 (jbe loc_4021AA)
     * Replace with: E9 2C 01 00 00 90 (jmp loc_4021AA; nop)
     * Ensures inserted coins remain safely in the machine without being prematurely ejected.
     */
    static const uint8_t jmp_bypass_coin_dwell[6] = {
        0xE9, 0x2C, 0x01, 0x00, 0x00, /* jmp loc_4021AA */
        0x90                          /* nop */
    };
    patch_memory(ADDR_COIN_DWELL_TIMEOUT_JUMP, jmp_bypass_coin_dwell, sizeof(jmp_bypass_coin_dwell));
    log_info(
        "Patched 30s coin dwell auto-payout jump at 0x%08X to unconditional bypass",
        ADDR_COIN_DWELL_TIMEOUT_JUMP);

    /* Patch 0x0044AD75: In CGameCommon::Init, initialize m_bShowTutorial to 0 instead of 1.
     * Originally: C6 86 30 05 00 00 01 (mov byte ptr [esi+530h], 1)
     * Replace with: C6 86 30 05 00 00 00 (mov byte ptr [esi+530h], 0)
     */
    static const uint8_t patch_common_no_tutorial[7] = { 0xC6, 0x86, 0x30, 0x05, 0x00, 0x00, 0x00 };
    patch_memory(ADDR_COMMON_INIT_TUTORIAL_FLAG, patch_common_no_tutorial, sizeof(patch_common_no_tutorial));

    /* Patch 0x004A270C: In CGameTitle_Update, prevent setting m_bShowTutorial ([g_pCGameCommon + 530h]) to 1.
     * Originally: C6 82 30 05 00 00 01 (mov byte ptr [edx+530h], 1)
     * Replace with: C6 82 30 05 00 00 00 (mov byte ptr [edx+530h], 0)
     * Prevents the game from popping up the un-timed tutorial screen when rotating from Title Attract to Panel mode!
     */
    static const uint8_t patch_title_no_tutorial[7] = { 0xC6, 0x82, 0x30, 0x05, 0x00, 0x00, 0x00 };
    patch_memory(ADDR_TITLE_SET_TUTORIAL_FLAG, patch_title_no_tutorial, sizeof(patch_title_no_tutorial));

    /* Patch 0x00473F2B: In CGameNormalMode_UpdateState, bypass tutorial popup check (state 10) directly to state 5.
     * Originally: 80 BE 30 05 00 00 00 74 32 (cmp byte ptr [esi+530h], 0; jz short loc_473F66)
     * Replace with: EB 39 90 90 90 90 90 90 90 (jmp short loc_473F66; 7x nop)
     * Guarantees returning from Title attract directly enters normal idle mode with working timeout!
     */
    static const uint8_t patch_panel_skip_tutorial[9] = {
        0xEB, 0x39, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    patch_memory(ADDR_PANEL_CHECK_TUTORIAL_FLAG, patch_panel_skip_tutorial, sizeof(patch_panel_skip_tutorial));
    log_info("Patched tutorial startup triggers at 0x%08X, 0x%08X, 0x%08X (manual tutorial retained)",
             ADDR_COMMON_INIT_TUTORIAL_FLAG, ADDR_TITLE_SET_TUTORIAL_FLAG, ADDR_PANEL_CHECK_TUTORIAL_FLAG);

    /* Patch 0x00403563 & 0x0040356C: In sub_403550, un-gate CMainApplication state checks.
     * Originally: 74 2F (jz 0x403594) and 74 26 (jz 0x403594).
     * Replace with 90 90 (nop nop).
     * Guarantees sub_401930 and accounting loops always process transfers & payouts!
     */
    static const uint8_t nops2[2] = { 0x90, 0x90 };
    patch_memory(ADDR_MAINAPP_UPDATE_GATE_1, nops2, sizeof(nops2));
    patch_memory(ADDR_MAINAPP_UPDATE_GATE_2, nops2, sizeof(nops2));

    /* Patch 0x00401C7B: In sub_401930, remove credit limit conditional jump on transfer.
     * Originally: 0F 8C 64 01 00 00 (jl loc_401DE5, 6 bytes).
     * Replace with 6x 0x90 (nop).
     * Ensures player can always transfer coins to credits without being blocked by limits.
     */
    patch_memory(ADDR_TRANSFER_CREDIT_LIMIT_JUMP, nops6, sizeof(nops6));

    /* Patch 0x00402233: In sub_401930, remove payout allow gate (lpCriticalSectionr+266).
     * Originally: 0F 84 6E 01 00 00 (jz loc_4023A7, 6 bytes).
     * Replace with 6x 0x90 (nop).
     * Guarantees Collect / Payout button immediately triggers payout whenever player has credits.
     */
    patch_memory(ADDR_COLLECT_STATE_GATE_JUMP, nops6, sizeof(nops6));

    /* Patch 0x004022FB: In sub_401930, eliminate debounce check and jump (cmp eax, 5; jb loc_4023D7).
     * Originally: 83 F8 05 0F 82 D3 00 00 00 (9 bytes).
     * Replace with: 9x 0x90 (nop).
     * Makes the Collect / Payout button trigger IMMEDIATELY on the very first frame!
     */
    static const uint8_t nops9[9] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    patch_memory(ADDR_COLLECT_DEBOUNCE_CMP, nops9, sizeof(nops9));

    /* Patch 0x004022BC: In sub_401930, bypass sub_4018A0 hopper check and Error 0xE800.
     * Originally: 75 37 (jnz short loc_4022F5).
     * When sub_4018A0 returns 0 (hopper disabled), it falls through to 0x004022E1 which calls
     * sub_4232D0(0, 0xE800) and sets g_CMainApplication_0+248 = 2 (error state, freezing all buttons).
     * Replacing with EB 37 (jmp short loc_4022F5) unconditionally jumps to loc_4022F5,
     * ensuring Collect / Payout proceeds directly without error 0xE800.
     */
    static const uint8_t jmp_bypass_hopper_check[2] = { 0xEB, 0x37 };
    patch_memory(ADDR_COLLECT_NO_HOPPER_JUMP, jmp_bypass_hopper_check, sizeof(jmp_bypass_hopper_check));

    /* Patch 0x00402321: In sub_401930, bypass hopper overflow check jump to Error 0xE800.
     * Originally: 74 15 (jz short loc_402338).
     * Replace with: EB 15 (jmp short loc_402338).
     * Prevents any overflow error branch from calling sub_4232D0(0, 0xE800).
     */
    static const uint8_t jmp_bypass_overflow_check[2] = { 0xEB, 0x15 };
    patch_memory(ADDR_COLLECT_OVERFLOW_JUMP, jmp_bypass_overflow_check, sizeof(jmp_bypass_overflow_check));

    /* Patch 0x004029C8: In sub_401930, bypass partial payout completion check jump to Error 0xE800.
     * Originally: 0F 84 35 02 00 00 (jz loc_402C03).
     * Replace with: E9 36 02 00 00 90 (jmp loc_402C03; nop).
     * Unconditionally transitions to Step 200 clean exit without calling sub_4232D0(0, 0xE800).
     */
    static const uint8_t jmp_bypass_complete_err[6] = {
        0xE9, 0x36, 0x02, 0x00, 0x00, /* jmp loc_402C03 */
        0x90                          /* nop */
    };
    patch_memory(ADDR_COLLECT_COMPLETE_ERR_JUMP, jmp_bypass_complete_err, sizeof(jmp_bypass_complete_err));

    /* Patch 0x004024EB & 0x00402535: In sub_401930, NOP out emergency payout stop calls (call sub_5C93A0).
     * Prevents false pay-stop triggers while player is holding or pressing the Collect button.
     */
    static const uint8_t nops5[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    patch_memory(ADDR_PAYOUT_EMERGENCY_STOP_1, nops5, sizeof(nops5));
    patch_memory(ADDR_PAYOUT_EMERGENCY_STOP_2, nops5, sizeof(nops5));

    /* Patch 0x00402802: In sub_401930 case 2, remove step check jump (jnz loc_40290E) so that
     * any remaining unpaid delta is correctly deducted from player Credits (g_pMedalAccounting + 9592)
     * instead of being misrouted to coin temp meter + 9612.
     * Originally: 0F 85 06 01 00 00 (jnz loc_40290E, 6 bytes).
     * Replace with: 6x 0x90 (nop).
     */
    patch_memory(ADDR_PAYOUT_CASE2_STEP_JUMP, nops6, sizeof(nops6));

    /* Hook 0x004232D0: Central KPM Error Dispatcher (sub_4232D0).
     * Suppresses any attempt to put machine into Error State (g_CMainApplication_0+248 = 2 or 4),
     * completely eliminating button lockups and error freeze screens.
     */
    uint8_t jmp_err[6] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90 };
    uint32_t rel_err = (uint32_t) my_sub_4232D0 - (ADDR_KPM_ERROR_DISPATCHER + 5);
    memcpy(&jmp_err[1], &rel_err, sizeof(rel_err));
    patch_memory(ADDR_KPM_ERROR_DISPATCHER, jmp_err, sizeof(jmp_err));

    log_info(
        "Installed Transfer & Collect responsiveness, Error 0xE800 elimination, and pay-stop protection patches",
        ADDR_MAINAPP_UPDATE_GATE_1);

    /* Hook 0x00427050: Startup OS version check (sub_427050).
     * By default, the game reads c:\VERSION.TXT for OS_VER. On PC environments or without
     * game.conf, this file does not exist, causing "OS VERSION CHECK BAD" (Error 0xF780).
     * We populate the OS version string buffer and return 1 (OK).
     */
    patch_jmp(ADDR_OS_VERSION_CHECK, my_os_version_check);
    log_info("Hooked OS version check at 0x%08X to return OK (\"KONAMI OS 2012\")", ADDR_OS_VERSION_CHECK);

    if (cfg->boot_to_title) {
        /* In CGameStatus::Init (0x00425730):
         * 0x00425A60: 8B CF (mov ecx, edi) -> ecx takes NVRAM restored status (5 = slot)
         * We patch this with: 31 C9 (xor ecx, ecx)
         * So CGameStatus::Init starts directly in Mode 0 (CGameTitle / Attract OP & CM)
         * for both stations upon launch!
         */
        static const uint8_t xor_ecx_ecx[2] = { 0x31, 0xC9 };
        patch_memory(ADDR_BOOT_STATUS_INIT_MODE, xor_ecx_ecx, sizeof(xor_ecx_ecx));

        /* In sub_426370 (0x00426370, called by CGameStatus::Start):
         * 0x004263C1: 74 6E (jz short loc_426431)
         * If the NVRAM first-boot byte [ecx+8] is non-zero, it calls sub_4545D0 which
         * forcefully transitions both stations to Mode 5 (Slot) on frame 0!
         * We patch 0x004263C1 with: EB 6E (jmp short loc_426431)
         * to skip sub_4545D0, allowing CGameTitle::Start() to run smoothly on launch!
         */
        static const uint8_t jmp_skip_startup_slot[2] = { 0xEB, 0x6E };
        patch_memory(ADDR_STARTUP_MODE_SWITCH_JUMP, jmp_skip_startup_slot, sizeof(jmp_skip_startup_slot));

        log_info(
            "Patched CGameStatus::Init (0x%08X) and sub_426370 (0x%08X) to boot cleanly into Mode 0 (Title / Attract OP)",
            ADDR_BOOT_STATUS_INIT_MODE,
            ADDR_STARTUP_MODE_SWITCH_JUMP);
    }


    /* Patch 0x00504695: In sub_504680, NOP out `jz loc_5049A0` (6 bytes: 0F 84 05 03 00 00)
     * so that whether NO_SUBBOARD is 0 or 1, backup memory mapping always targets the
     * internal SRAM buffer at 0x01D02970 instead of querying physical PLX 9030 BAR2 memory.
     */
    static const uint8_t nops6_nvram[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    patch_memory(ADDR_NVRAM_SUBBOARD_BRANCH, nops6_nvram, sizeof(nops6_nvram));
    log_info(
        "Patched NVRAM subboard branch at 0x%08X to NOPs (forces SRAM buffer at 0x%08X)",
        ADDR_NVRAM_SUBBOARD_BRANCH,
        ADDR_NVRAM_SRAM_BUFFER);

    /* Hook sub_504680 memset call to load battery SRAM NVRAM from disk */
    patch_call(ADDR_NVRAM_MEMSET_CALL, kpm_nvram_init);
    log_info("Hooked NVRAM memset at 0x%08X to kpm_nvram_init", ADDR_NVRAM_MEMSET_CALL);


    init_light_serial(cfg);

    switch (cfg->cabinet_girl) {
        case KPMHOOK_CABINET_GIRL_RINKO:
            s_cabinet_jumper_byte = 0x01; /* Bit 0: Rinko (Kanojyo ID 1) */
            log_info("PCSub cabinet hardware jumper: RINKO (Jumper=0x01, GirlID=1)");
            break;
        case KPMHOOK_CABINET_GIRL_NENE:
            s_cabinet_jumper_byte = 0x04; /* Bit 2: Nene (Kanojyo ID 2) */
            log_info("PCSub cabinet hardware jumper: NENE (Jumper=0x04, GirlID=2)");
            break;
        case KPMHOOK_CABINET_GIRL_MANAKA:
        default:
            s_cabinet_jumper_byte = 0x02; /* Bit 1: Manaka (Kanojyo ID 0) */
            log_info("PCSub cabinet hardware jumper: MANAKA (Jumper=0x02, GirlID=0)");
            break;
    }

    s_io_initialized = true;
    log_info("PCSub arcade I/O and lighting subsystem initialized successfully");
}

void kpm_io_hook_update(void)
{
    if (!s_io_initialized) {
        return;
    }

    static bool s_backend_ready = false;
    if (!s_backend_ready) {
        s_backend_ready = true;
        log_info("Initializing kpmio backend outside loader lock...");
        kpm_io_set_loggers(
            log_impl_misc,
            log_impl_info,
            log_impl_warning,
            log_impl_fatal);

        if (!kpm_io_init(
                crt_thread_create, crt_thread_join, crt_thread_destroy)) {
            log_warning("kpm_io_init returned false; falling back to direct emulation");
        } else {
            log_info("kpmio backend initialized successfully");
        }
    }

    kpm_io_read_inputs();

    uint32_t *p_subwrap = (uint32_t *) ADDR_DWORD_1ACFCB8;
    if (!p_subwrap) {
        return;
    }

    uint8_t *subwrap_base = (uint8_t *) (*p_subwrap);
    if (!subwrap_base) {
        return;
    }

    /* Offset 336 (0x150): Hardware Button bitmask (Channels 0..11) */
    uint16_t btns = kpm_io_get_buttons();
    uint32_t btn_mask = (uint32_t) btns;

    /* Trace Transfer key transitions with real-time coin & credit balances */
    static bool s_prev_transfer_state = false;
    bool curr_transfer_pressed = (btn_mask & KPM_IO_BTN_TRANSFER) != 0;
    uint8_t *crit = *((uint8_t **) 0x01ACE788);

    if (curr_transfer_pressed && !s_prev_transfer_state) {
        uint32_t *p_data = (uint32_t *) 0x00E652D4;
        uint32_t coins_avail = (p_data && *p_data) ? *((uint32_t *) (*p_data + 9608)) : 0;
        uint32_t credits_avail = (p_data && *p_data) ? *((uint32_t *) (*p_data + 9592)) : 0;
        log_info(
            "Transfer key pressed! coin_meter=%u, credits=%u, busy=%d, step=%u, latch=%d, debounce=%u",
            coins_avail,
            credits_avail,
            crit ? crit[244] : -1,
            crit ? *((uint16_t *) (crit + 246)) : 0xFFFF,
            crit ? crit[276] : -1,
            crit ? *((uint32_t *) (crit + 280)) : 0xFFFFFFFF);
    }
    s_prev_transfer_state = curr_transfer_pressed;

    /* When Transfer key is released, actively clear transfer latch [crit+276] and debounce [crit+280] */
    if (!curr_transfer_pressed && crit) {
        crit[276] = 0;
        *((uint32_t *) (crit + 280)) = 0;
    }

    /* Trace Collect key transitions */
    static bool s_prev_collect_state = false;
    bool curr_collect_pressed = (btn_mask & KPM_IO_BTN_COLLECT_PAYOUT) != 0;
    if (curr_collect_pressed && !s_prev_collect_state) {
        uint32_t *p_data = (uint32_t *) 0x00E652D4;
        uint32_t coins_avail = (p_data && *p_data) ? *((uint32_t *) (*p_data + 9608)) : 0;
        uint32_t credits_avail = (p_data && *p_data) ? *((uint32_t *) (*p_data + 9592)) : 0;
        log_info(
            "Collect key pressed! coin_meter=%u, credits=%u, busy=%d, step=%u, latch=%d, debounce=%u",
            coins_avail,
            credits_avail,
            crit ? crit[244] : -1,
            crit ? *((uint16_t *) (crit + 246)) : 0xFFFF,
            crit ? crit[265] : -1,
            crit ? *((uint32_t *) (crit + 256)) : 0xFFFFFFFF);
    }
    s_prev_collect_state = curr_collect_pressed;

    /* When Collect key is released, actively clear collect latch [crit+265] and debounce [crit+256] */
    if (!curr_collect_pressed && crit) {
        crit[265] = 0;
        *((uint32_t *) (crit + 256)) = 0;
    }

    /* Ensure payout allow flag [crit+266] is ALWAYS active (0xFF) to prevent false pay-stops */
    if (crit) {
        crit[299] = 1; /* Hopper enabled override for sub_4018A0 */
        crit[266] = 0xFF; /* Payout allow flag ALWAYS active */
    }

    /* Synchronously write hardware buttons to BOTH subwrap + 336 and subboard + 4 */
    *((uint32_t *) (subwrap_base + 336)) = btn_mask;

    uint32_t *p_subboard = (uint32_t *) ADDR_DWORD_1ACFCBC;
    uint8_t *subboard_base = (p_subboard && *p_subboard) ? (uint8_t *) (*p_subboard) : NULL;
    if (subboard_base) {
        *((uint32_t *) (subboard_base + 4)) = btn_mask;
    }

    /* Offset 946 (0x3B2): PCSub Cabinet Girlfriend Jumper bitmask
     * Bit 1 (0x02) = Manaka (Kanojyo ID 0)
     * Bit 0 (0x01) = Rinko  (Kanojyo ID 1)
     * Bit 2 (0x04) = Nene   (Kanojyo ID 2)
     */
    subwrap_base[946] = s_cabinet_jumper_byte;

    if (subboard_base) {
        subboard_base[616] = s_cabinet_jumper_byte;

        /* Virtualize PCSub DS2430A authentic dongles */
        static bool s_dongles_injected = false;
        if (!s_dongles_injected && (subboard_base[101] != 5 || subboard_base[143] != 5)) {
            init_ds2430a_dongles();

            /* Device 0: Black Software Plug */
            subboard_base[16] = 0; /* dev */
            subboard_base[17] = 0; /* status = OK */
            memcpy(&subboard_base[18], s_soft_dongle.rom_id, 8);
            memcpy(&subboard_base[26], s_soft_dongle.payload, 32);

            /* Device 1: White Network Plug */
            subboard_base[58] = 1; /* dev */
            subboard_base[59] = 0; /* status = OK */
            memcpy(&subboard_base[60], s_net_dongle.rom_id, 8);
            memcpy(&subboard_base[68], s_net_dongle.payload, 32);

            typedef char (__stdcall *check_dongles_fn_t)(uint8_t *subboard);
            check_dongles_fn_t p_check_dongles = (check_dongles_fn_t) ADDR_PCSUB_CHECK_DONGLES_FUNC;
            p_check_dongles(subboard_base);

            s_dongles_injected = true;
            log_info(
                "Injected authentic DS2430A dongles into PCSub: SoftPlug status=%d, NetPlug status=%d, PCBID=%s",
                subboard_base[101],
                subboard_base[143],
                (const char *) (subboard_base + 216));
        }
    }

    /* Ensure AGING MODE is clear:
     * *(g_pStationCtx_0 + 144): AGING MODE (must be 0 to avoid auto-play / auto-credits).
     */
    uint32_t *p_ctx0 = (uint32_t *) ADDR_STATION_CTX_0;
    if (p_ctx0 && *p_ctx0) {
        uint8_t *ctx0 = (uint8_t *) (*p_ctx0);
        ctx0[144] = 0;
        ctx0[40] = 1; /* Station eamuse enable */
        ctx0[42] = 1;
        ctx0[14] = 1; /* Medal payout enable for sub_4018A0 */
        ctx0[13] = 0; /* Disable pay-stop abort button */
        ctx0[12] = 0; /* Clear hopper overflow error flag */
    }
    uint32_t *p_ctx1 = (uint32_t *) ADDR_STATION_CTX_1;
    if (p_ctx1 && *p_ctx1) {
        uint8_t *ctx1 = (uint8_t *) (*p_ctx1);
        ctx1[144] = 0;
        ctx1[14] = 1;
        ctx1[13] = 0;
        ctx1[12] = 0;
    }
    uint32_t *p_ctx2 = (uint32_t *) ADDR_STATION_CTX_2;
    if (p_ctx2 && *p_ctx2) {
        uint8_t *ctx2 = (uint8_t *) (*p_ctx2);
        ctx2[144] = 0;
        ctx2[14] = 1;
        ctx2[13] = 0;
        ctx2[12] = 0;
    }

    /* Keep CEamuseControl station enable active (natural network status driven by eam_if) */
    uint32_t *p_eamuse = (uint32_t *) ADDR_GLOBAL_EAMUSE_CONTROL;
    if (p_eamuse && *p_eamuse) {
        uint8_t *eam_base = (uint8_t *) (*p_eamuse);
        eam_base[101] = 1; /* Station eamuse enable */
    }

    /* Ensure subboard ready flag (subwrap + 284) is active so sub_5C9C40 runs all state machines */
    *((uint16_t *) (subwrap_base + 284)) = 1;

    /* Ensure CMainApplication gates (+228 and +229) are active so sub_403550 processes transfer */
    uint32_t *p_mainapp = (uint32_t *) 0x01ACE78C;
    if (p_mainapp && *p_mainapp) {
        uint8_t *mainapp = (uint8_t *) (*p_mainapp);
        mainapp[228] = 1;
        mainapp[229] = 1;
    }

    /* Feed hardware buttons to subboard raw input buffer (offset 4) */
    if (subboard_base) {
        *((uint32_t *) (subboard_base + 4)) = btn_mask;
    }

    /* Offset 476 (0x1DC): MEDAL IN pulse counter
     * Offset 296..298: Direct CPcSubWrap medal ingestion for sub_403230
     */
    uint16_t medals = kpm_io_get_medal_pulse();
    if (medals > 0) {
        static uint8_t s_medal_seq = 0;
        s_medal_seq++;
        if (s_medal_seq == 0) {
            s_medal_seq = 1;
        }

        /* 1. Direct ingestion into CPcSubWrap: consumed by sub_403230 to call sub_401610(medals * rate, 0) */
        subwrap_base[297] = s_medal_seq;
        *((uint16_t *) (subwrap_base + 298)) = medals;
        subwrap_base[296] = 1;

        /* 2. Update subwrap + 476 and subboard hardware registers for Test Mode verification */
        *((uint16_t *) (subwrap_base + 476)) += medals;
        if (subboard_base) {
            subboard_base[619] = 1;
            subboard_base[620] = 1;
            subboard_base[621] = s_medal_seq;
            *((uint16_t *) (subboard_base + 622)) = medals;

            subboard_base[632] = 1;
            subboard_base[633] = s_medal_seq;
            *((uint16_t *) (subboard_base + 634)) = medals;
            subboard_base[625] = 0;
            *((uint32_t *) (subboard_base + 628)) += 1;
        }
        log_info("Dispatched %u medal pulse(s) to CPcSubWrap (offset 296, seq=%u)", medals, s_medal_seq);
    }

    /* Offset 508 (0x1FC): COIN IN pulse counter
     * Offset 300..302: Direct CPcSubWrap coin ingestion for sub_403230
     */
    uint16_t coins = kpm_io_get_coin_pulse();
    if (coins > 0) {
        static uint8_t s_coin_seq = 0;
        s_coin_seq++;
        if (s_coin_seq == 0) {
            s_coin_seq = 1;
        }

        /* 1. Direct ingestion into CPcSubWrap: consumed by sub_403230 to call sub_401610(coins, 4),
         * which increments 100-yen coin balance in g_pMedalAccounting + 9608 so Transfer button works! */
        subwrap_base[301] = s_coin_seq;
        *((uint16_t *) (subwrap_base + 302)) = coins;
        subwrap_base[300] = 1;

        /* 2. Update subwrap + 508 and subboard hardware registers for Test Mode verification */
        *((uint16_t *) (subwrap_base + 508)) += coins;
        if (subboard_base) {
            subboard_base[652] = 1;
            subboard_base[653] = 1;
            subboard_base[654] = s_coin_seq;
            *((uint16_t *) (subboard_base + 656)) = coins;

            subboard_base[664] = 1;
            subboard_base[665] = s_coin_seq;
            *((uint16_t *) (subboard_base + 666)) = coins;
            subboard_base[659] = 0;
            *((uint32_t *) (subboard_base + 660)) += 1;
        }
        log_info("Dispatched %u coin pulse(s) to CPcSubWrap (offset 300, seq=%u)", coins, s_coin_seq);
    }

    /* Check coin meter directly from medal accounting (0x00E652D4 + 0x2588) */
    uint32_t coin_meter = 0;
    uint32_t *p_medal_acct = (uint32_t *) 0x00E652D4;
    if (p_medal_acct && *p_medal_acct) {
        uint8_t *acct = (uint8_t *) (*p_medal_acct);
        coin_meter = *((uint32_t *) (acct + 0x2588));
    }

    /* If player operates buttons, coins, medals, touches the screen,
     * OR holds un-transferred coins in the coin meter: keep idle timers alive! */
    if (btn_mask != 0 || medals > 0 || coins > 0 || coin_meter > 0 || kpm_touch_get_and_clear_activity()) {
        reset_game_idle_timers();
    }

    /* --- Virtual Hopper Engine (Payout closed-loop simulation) ---
     * Addresses:
     *   subwrap + 420: uint16_t state (0=Idle, 10=ReqSent, 20=WaitAck, 50=PayingOut, 100=Error)
     *   subwrap + 422: uint16_t requested payout amount
     *   subwrap + 424: uint16_t current paid count (displayed in game UI)
     *   subwrap + 432: uint32_t payout transaction tag / sequence
     *   subwrap + 436: uint32_t status seq
     *   subwrap + 444: uint32_t result status (1=PayingOut, 2=Complete, 3=Empty, 4=Jammed)
     *
     * In subboard_base (dword_1ACFCBC):
     *   subboard + 691: uint8_t status (1=PayingOut, 2=Complete)
     *   subboard + 692: uint16_t paid count
     *   subboard + 696: uint32_t response seq
     *   subboard + 700: uint8_t motor status (1=Running, 0=Stopped)
     *   subboard + 701: uint8_t optical sensor (1=ON / Coin detected, 0=OFF)
     *   subboard + 702: uint8_t overall status (0=NORMAL)
     *   subboard + 704: uint32_t test mode query response seq
     *   subboard + 708: uint16_t error code (0=No error)
     *   subboard + 712: uint32_t error seq
     */
    uint16_t req_payout = *((uint16_t *) (subwrap_base + 422));
    uint16_t hopper_state = *((uint16_t *) (subwrap_base + 420));

    /* 1. Capture new payout request whenever sub_5C9330 writes to +422 or state is 10/20 */
    if (req_payout > 0) {
        log_info("Virtual Hopper: Captured payout demand for %u medals (hopper_state=%u)",
                 req_payout, hopper_state);
        kpm_io_payout_demand(req_payout);
        *((uint16_t *) (subwrap_base + 422)) = 0;
        *((uint16_t *) (subwrap_base + 420)) = 50;
        *((uint32_t *) (subwrap_base + 444)) = 1; /* 1 = PayingOut */
        *((uint16_t *) (subwrap_base + 424)) = 0;
        hopper_state = 50;
    }

    /* 2. Synchronize virtual hopper hardware registers with simulation */
    uint16_t paid_count = 0;
    bool motor_running = false;
    bool sensor_active = false;
    uint8_t h_status = kpm_io_get_payout_detail(&paid_count, &motor_running, &sensor_active);

    static bool s_logged_complete = false;

    if (h_status == 1) {
        /* Actively dispensing medals */
        s_logged_complete = false;
        reset_game_idle_timers();
        *((uint16_t *) (subwrap_base + 420)) = 50;
        *((uint16_t *) (subwrap_base + 424)) = paid_count;
        *((uint32_t *) (subwrap_base + 444)) = 1;

        if (subboard_base) {
            subboard_base[700] = 1;
            subboard_base[701] = sensor_active ? 1 : 0;
            subboard_base[702] = 0;
            subboard_base[691] = 1;
            *((uint16_t *) (subboard_base + 692)) = paid_count;
            *((uint32_t *) (subboard_base + 696)) += 1;
            *((uint32_t *) (subboard_base + 704)) += 1;
        }
    } else if (h_status == 2) {
        /* Dispensing complete */
        if (!s_logged_complete) {
            log_info("Virtual Hopper: Completed dispensing %u medals! Awaiting game state machine finish...", paid_count);
            s_logged_complete = true;
        }
        *((uint16_t *) (subwrap_base + 420)) = 0;
        *((uint16_t *) (subwrap_base + 424)) = paid_count;
        *((uint32_t *) (subwrap_base + 444)) = 2; /* 2 = Complete */

        if (subboard_base) {
            subboard_base[700] = 0;
            subboard_base[701] = 0;
            subboard_base[702] = 0;
            subboard_base[691] = 2;
            *((uint16_t *) (subboard_base + 692)) = paid_count;
            *((uint32_t *) (subboard_base + 696)) += 1;
            *((uint32_t *) (subboard_base + 704)) += 1;
        }
    } else {
        /* Hopper idle */
        *((uint16_t *) (subwrap_base + 420)) = 0; /* Keep idle so sub_5C9330 always accepts requests */
        if (subboard_base) {
            subboard_base[700] = 0;
            subboard_base[701] = 0;
            subboard_base[702] = 0;
            *((uint32_t *) (subboard_base + 704)) += 1;
        }
    }

    /* 3. Payout State Guard & Failsafe Watchdog */
    if (crit) {
        uint16_t payout_step = *((uint16_t *) (crit + 246));
        uint32_t result_stat = *((uint32_t *) (subwrap_base + 444));

        /* Handshake closure: When game returns to payout_step == 0 after completing payout (result_stat == 2),
         * cleanly reset result status, hopper state, and kpmio demand */
        if (payout_step == 0) {
            if (result_stat == 2) {
                *((uint32_t *) (subwrap_base + 444)) = 0;
                *((uint16_t *) (subwrap_base + 420)) = 0;
                kpm_io_payout_demand(0);
                log_info("Virtual Hopper: Handshake closed cleanly (payout_step=0, result_stat=0)");
            }
        }

        /* Progress-based stuck payout failsafe watchdog:
         * Only triggers if payout is stuck with NO medal progress for > 15s */
        static DWORD s_last_progress_tick = 0;
        static uint16_t s_last_paid_check = 0xFFFF;
        if (payout_step > 0) {
            if (s_last_progress_tick == 0 || paid_count != s_last_paid_check) {
                s_last_progress_tick = GetTickCount();
                s_last_paid_check = paid_count;
            } else if (GetTickCount() - s_last_progress_tick > 15000) {
                log_warning(
                    "Payout watchdog: auto-clearing truly stuck payout_step %u (paid=%u) after 15s no progress",
                    payout_step,
                    paid_count);
                crit[244] = 0;
                *((uint16_t *) (crit + 246)) = 0;
                *((uint16_t *) (crit + 292)) = 0;
                crit[265] = 0;
                *((uint32_t *) (crit + 256)) = 0;
                *((uint16_t *) (subwrap_base + 420)) = 0;
                *((uint16_t *) (subwrap_base + 422)) = 0;
                *((uint32_t *) (subwrap_base + 444)) = 0;
                kpm_io_payout_demand(0);
                s_last_progress_tick = 0;
                s_last_paid_check = 0xFFFF;
            }
        } else {
            s_last_progress_tick = 0;
            s_last_paid_check = 0xFFFF;
        }

        /* Keep residual payout accounting clean when machine is not paying out */
        if (payout_step == 0) {
            uint32_t *p_acct = (uint32_t *) 0x00E652D4;
            if (p_acct && *p_acct) {
                uint8_t *acct = (uint8_t *) (*p_acct);
                *((uint32_t *) (acct + 9612)) = 0;
                *((uint32_t *) (acct + 9860)) = 0;
            }
        }
    }

    /* Drain the PCSub serial command queue at offset 532 (32 slots of 12 bytes) */
    uint16_t head = *((uint16_t *) (subwrap_base + 528)) & 0x1F;
    uint16_t tail = *((uint16_t *) (subwrap_base + 530)) & 0x1F;

    while (tail != head) {
        tail = (tail + 1) & 0x1F;
        uint32_t *cmd = (uint32_t *) (subwrap_base + 532 + 12 * tail);
        uint32_t code = cmd[0] & 0xFFFF;
        uint32_t p1 = cmd[1];
        uint32_t p2 = cmd[2];

        log_info("Cabinet illumination command: code=%u, p1=%u, p2=0x%08X", code, p1, p2);

        if (s_light_serial != INVALID_HANDLE_VALUE) {
            uint32_t packet[3] = { code, p1, p2 };
            DWORD written = 0;
            WriteFile(s_light_serial, packet, sizeof(packet), &written, NULL);
        }
    }
    *((uint16_t *) (subwrap_base + 530)) = head;

    /* Direct hardware discrete lamps: Offsets 356..366 (11 discrete channels) */
    uint32_t lamp_bits = 0;
    for (int i = 0; i < 11; i++) {
        uint8_t state = subwrap_base[356 + i];
        if (state != 2 && state != 0) {
            lamp_bits |= (1 << i);
        }
    }

    static uint32_t s_last_logged_lamp_bits = 0xFFFFFFFF;
    if (lamp_bits != s_last_logged_lamp_bits) {
        log_info(
            "Lamp bits changed: 0x%03X (Btns: 0x%02X, POP: 0x%02X)",
            lamp_bits,
            lamp_bits & 0x1F,
            (lamp_bits >> 5) & 0x3F);
        s_last_logged_lamp_bits = lamp_bits;
    }

    kpm_io_set_lamps(lamp_bits);

    /* Periodically persist battery-backed SRAM NVRAM if modified */
    DWORD now = GetTickCount();
    if (now - s_last_nvram_sync_tick >= 2000) {
        s_last_nvram_sync_tick = now;
        kpm_nvram_flush();
    }
}

void kpm_io_hook_fini(void)
{
    log_info("Flushing battery-backed SRAM NVRAM and finalizing I/O subsystems...");
    kpm_nvram_flush();

    if (s_light_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(s_light_serial);
        s_light_serial = INVALID_HANDLE_VALUE;
    }
}


