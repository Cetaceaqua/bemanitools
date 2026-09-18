#define LOG_MODULE "kpm-io-hook"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "bemanitools/kpmio.h"
#include "kpmhook/config-io.h"
#include "kpmhook/io-hook.h"
#include "util/log.h"
#include "util/thread.h"

#define ADDR_KEYBOARD_LOOP_START  0x00411686
#define ADDR_KEYBOARD_LOOP_END    0x00411C56
#define ADDR_SUBBOARD_CHECK_JUMP  0x00411CC4
#define ADDR_DWORD_1ACFCB8        0x01ACFCB8
#define ADDR_DWORD_1ACFCBC        0x01ACFCBC
#define ADDR_CHECK_PCSUB_MODE_JUMP   0x0041ECF7
#define ADDR_NO_SUBBOARD_CHECK_CALL  0x0041F4DE
#define ADDR_BOOT_CREDITS_VALUE   0x0041F5E3
#define ADDR_CHECK_EAMUSE_PLUG_JUMP  0x0041FDC6
#define ADDR_PCSUB_GET_NET_PLUG_ID   0x005CA870
#define ADDR_HOPPER_PAY_STOP_FUNC    0x005C93A0
#define ADDR_SECRET_MENU_COUNT_DEC   0x0042C199
#define ADDR_SECRET_MENU_DRAW_FILTER 0x0042C550
#define ADDR_ATTRACT_TIMEOUT_1       0x00473670
#define ADDR_ATTRACT_TIMEOUT_2       0x00473DA3
#define ADDR_ATTRACT_SKIP_JUMP       0x00473D90
#define ADDR_ATTRACT_CREDIT_BYPASS   0x00473DA9
#define ADDR_SLOT_IDLE_TIMER_RESET   0x00471362
#define ADDR_STEP39_ABORT_CHECK      0x0047BCE0
#define ADDR_BETSLOT_SKIP_JUMP       0x004C43FB
#define ADDR_BETSLOT_TIMEOUT         0x004C4412
#define ADDR_BETSLOT_CREDIT_BYPASS   0x004C4440
#define ADDR_BOOT_STATUS_INIT_MODE   0x00425A60
#define ADDR_STARTUP_MODE_SWITCH_JUMP 0x004263C1
#define ADDR_HARDWARE_TEST_MODE_0    0x004F2DC8
#define ADDR_HARDWARE_TEST_MODE_1    0x004F2DE8
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

#define TOTAL_LEDS                110
#define NUM_STRIPS                11
#define LEDS_PER_STRIP            10

/* Strip layout:
 * Strips 0..2  (LEDs 0..29):   SCREEN LED 01..03
 * Strips 3..4  (LEDs 30..49):  FRONT LED LEFT, RIGHT
 * Strips 5..7  (LEDs 50..79):  SIDE LED LEFT 01..03
 * Strips 8..10 (LEDs 80..109): SIDE LED RIGHT 01..03
 */

#pragma pack(push, 1)
struct rgb_val {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
#pragma pack(pop)

static bool s_io_initialized = false;
static struct kpmhook_config_io s_cfg;
static HANDLE s_raw_serial_handle = INVALID_HANDLE_VALUE;

/* 110 LED color buffer */
static struct rgb_val s_led_buffer[TOTAL_LEDS];
static uint32_t s_current_anim_code = 8; /* Default to normal play lighting */
static DWORD s_anim_start_tick = 0;
static struct rgb_val s_heroine_color = { 30, 144, 255 }; /* Default Manaka Sky Blue */
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

static void init_raw_serial(const struct kpmhook_config_io *cfg)
{
    if (!cfg->lights_raw_serial) {
        return;
    }

    char port_path[64];
    snprintf(port_path, sizeof(port_path), "\\\\.\\%s", cfg->lights_raw_port);

    s_raw_serial_handle = CreateFileA(
        port_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (s_raw_serial_handle == INVALID_HANDLE_VALUE) {
        log_warning(
            "Could not open raw serial LED port '%s' (Error %lu)",
            cfg->lights_raw_port,
            GetLastError());
        return;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(s_raw_serial_handle, &dcb)) {
        dcb.BaudRate = cfg->lights_raw_baud > 0 ? cfg->lights_raw_baud : CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(s_raw_serial_handle, &dcb);
    }

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 10;
    SetCommTimeouts(s_raw_serial_handle, &timeouts);

    log_info(
        "Raw serial LED stream active on %s at %d baud (110 LEDs / 330 bytes payload)",
        cfg->lights_raw_port,
        cfg->lights_raw_baud);
}

static uint32_t *get_slot_idle_timer(void)
{
    uint32_t *p_app = (uint32_t *) 0x01ACE78C;
    if (!p_app || !*p_app) return NULL;
    uint8_t *app = (uint8_t *) (*p_app);
    uint32_t *p_st0 = (uint32_t *) (app + 0x8C);
    if (!p_st0 || !*p_st0) return NULL;
    uint8_t *st0 = (uint8_t *) (*p_st0);
    uint32_t *vtable = (uint32_t *) (*((uint32_t *) st0));
    if (!vtable) return NULL;
    void *fn = (void *) vtable[0x64 / 4];
    uint8_t *status = NULL;
    __asm {
        mov ecx, st0
        call fn
        mov status, eax
    }
    if (!status) return NULL;
    uint32_t current_mode = *((uint32_t *) (status + 0x77418));
    if (current_mode == 5) {
        return (uint32_t *) (status + 0x247C + 0x70A78);
    }
    if (current_mode == 11) {
        /* Mode 11 CGameBetSlot is at status + 0x76F48.
         * Offset +116 (0x74) is m_pSlotMain (CGameSlotMainBet).
         * Inside CGameSlotMainBet, offset +360 (0x168) is the idle timer. */
        uint8_t *betslot = status + 0x76F48;
        uint32_t *p_slotmain = (uint32_t *) (betslot + 116);
        if (p_slotmain && *p_slotmain) {
            return (uint32_t *) (*p_slotmain + 360);
        }
    }
    return NULL;
}

static const uintptr_t SUB_5C93A0_CONT = 0x005C93AB;

static void my_sub_5C93A0_impl(void)
{
    log_info("sub_5C93A0 intercepted: Emergency stopping virtual hopper payout");
    kpm_io_payout_stop();
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

static const char S_KONAMI_PCB_ID[] = "014014000003CCCBC5E6";

/* Standard 40-byte DS2430A Dongle structure (32B Payload + 8B ROM ID) */
struct ds2430a_dongle {
    uint8_t rom_id[8];
    uint8_t payload[32];
};

/* Default authenticated DS2430A White Network Plug (E-AMUSE3, @@@@@@@@, PCBID: 014014000003CCCBC5E6) */
static const uint8_t S_DEFAULT_WHITE_ROM_ID[8] = {
    0x14, 0x00, 0x00, 0x03, 0xCC, 0xCB, 0xC5, 0xE6
};
static const uint8_t S_DEFAULT_WHITE_PAYLOAD[32] = {
    0x5D, 0x7A, 0xD2, 0xCC, 0xAE, 0x64, 0x20, 0x08,
    0x82, 0x20, 0x08, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60
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
 * sub_5499B0 CEamuseControl::GetStatus(void)
 * __thiscall calling convention (ECX = this).
 * Returns `this + 104` (pointer to 176-byte EAMUSE_STATUS struct).
 * We intercept this to guarantee network_status = 1 (ONLINE) and service_status = 2 (AVAILABLE),
 * completely banishing LOGO_C and top offline warning banners.
 */
static __declspec(naked) char *my_Eamuse_GetStatus(void)
{
    __asm {
        lea eax, [ecx+68h]          ; eax = this + 104
        mov word ptr [eax+0A8h], 1  ; network_status = 1 (ONLINE)
        mov word ptr [eax+0AAh], 2  ; service_status = 2 (AVAILABLE)
        mov dword ptr [eax+0A0h], 0 ; error_code = 0 (NO_ERROR)
        retn
    }
}

/*
 * sub_5CA870 CPcSubWrap::GetNetPlugPcbId(char *buf, unsigned int max_len)
 * __thiscall calling convention (ECX = this, [esp+4] = buf, [esp+8] = max_len).
 * Returns `buf` on success, pops 8 bytes of stack arguments upon return.
 */
static __declspec(naked) int my_sub_5CA870(void)
{
    __asm {
        mov eax, [esp+4]       ; char *buf
        test eax, eax
        jz _ret
        mov ecx, [esp+8]       ; unsigned int max_len
        test ecx, ecx
        jz _ret
        push esi
        push edi
        mov edi, eax
        mov esi, offset S_KONAMI_PCB_ID
_copy_loop:
        dec ecx
        jz _null_term
        mov dl, [esi]
        test dl, dl
        jz _null_term
        mov [edi], dl
        inc esi
        inc edi
        jmp _copy_loop
        _null_term:
        mov byte ptr [edi], 0
        pop edi
        pop esi
_ret:
        ret 8
    }
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

    /* Hook sub_5499B0 (CEamuseControl::GetStatus) to enforce ONLINE and AVAILABLE status */
    uint8_t jmp_get_status[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    uint32_t rel_status = (uint32_t) my_Eamuse_GetStatus - (ADDR_EAMUSE_GET_STATUS_FUNC + 5);
    memcpy(&jmp_get_status[1], &rel_status, sizeof(rel_status));
    patch_memory(ADDR_EAMUSE_GET_STATUS_FUNC, jmp_get_status, sizeof(jmp_get_status));

    /* Also hook CEamuseControl vftable[9] directly for double assurance */
    uint32_t new_vptr_9 = (uint32_t) my_Eamuse_GetStatus;
    patch_memory(ADDR_EAMUSE_VFTABLE + 9 * 4, (const uint8_t *) &new_vptr_9, sizeof(new_vptr_9));
    log_info(
        "Hooked CEamuseControl::GetStatus at 0x%08X (and vftable[9] 0x%08X) to eliminate LOGO_C banner",
        ADDR_EAMUSE_GET_STATUS_FUNC,
        ADDR_EAMUSE_VFTABLE + 9 * 4);

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

    /* Patch boot initial credit grant (sub_41F410).
     * By default the game gives 1000 credits on boot if NO_SUBBOARD is enabled.
     * Setting this to 0 ensures authentic arcade operating mode waiting for medals.
     */
    uint32_t boot_cred = (uint32_t) cfg->boot_credits;
    patch_memory(ADDR_BOOT_CREDITS_VALUE, (const uint8_t *) &boot_cred, sizeof(boot_cred));
    log_info("Configured boot initial credits: %u (at 0x%08X)", boot_cred, ADDR_BOOT_CREDITS_VALUE);

    /* Hook sub_41F410 CheckInitError NO_SUBBOARD check to protect SRAM settings */
    patch_call(ADDR_NO_SUBBOARD_CHECK_CALL, my_check_no_subboard);
    log_info(
        "Hooked NO_SUBBOARD initialization check at 0x%08X to protect SRAM persistence",
        ADDR_NO_SUBBOARD_CHECK_CALL);

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

        /* Prevent false-positive flags and credit balance from blocking idle attract timeout:
         * 1. sub_4532A0 and sub_453280 are patched to return 0 (xor eax, eax; ret).
         * 2. NOP out the jnz loc_473E16 at 0x00473D90 (6 bytes: 0F 85 80 00 00 00) so
         *    virtual e-Pass session flags (esi != 0) do not skip the idle timer evaluation.
         * 3. At 0x00473DA9, patch with jmp short loc_473DEC (EB 41) so when the idle timer expires,
         *    it bypasses the credit check at 0x00473DCE (which resets the timer if credits > 0)
         *    and jumps directly to loc_473DEC to transition to Step 39 (sub_47BAB0).
         * 4. Hook Slot Mode vtable update (0x00BD5650) to track CGameSlotMode instance and reset
         *    idle timer [ebx+70A78h] whenever player operates buttons, coins, or medals.
         * 5. Patch 0x0047BCE0 in sub_47BAB0 with jmp short loc_47BD4C (EB 6A) so card status
         *    checks do not falsely abort Step 39 during the 1.5-second fadeout before switching to Mode 0.
         */
        static const uint8_t ret_zero[3] = { 0x31, 0xC0, 0xC3 };
        patch_memory(ADDR_FUNC_SUB_4532A0, ret_zero, sizeof(ret_zero));
        patch_memory(ADDR_FUNC_SUB_453280, ret_zero, sizeof(ret_zero));

        static const uint8_t nops6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        patch_memory(ADDR_ATTRACT_SKIP_JUMP, nops6, sizeof(nops6));

        static const uint8_t jmp_to_step39[2] = { 0xEB, 0x41 };
        patch_memory(ADDR_ATTRACT_CREDIT_BYPASS, jmp_to_step39, sizeof(jmp_to_step39));

        static const uint8_t jmp_step39_safe[2] = { 0xEB, 0x6A };
        patch_memory(ADDR_STEP39_ABORT_CHECK, jmp_step39_safe, sizeof(jmp_step39_safe));

        log_info(
            "Unblocked idle attract timer (0x%08X), credit bypass (0x%08X), and Step 39 fadeout protector (0x%08X)",
            ADDR_ATTRACT_SKIP_JUMP,
            ADDR_ATTRACT_CREDIT_BYPASS,
            ADDR_STEP39_ABORT_CHECK);

        /* Also patch Mode 11 (CGameSlotMainBet) idle timeout:
         * 1. 0x004C4412: Patch 120s timeout threshold (1D4C0h) to configured timeout_ms.
         * 2. 0x004C43FB: Replace `test esi, esi; jz short loc_4C4403` with `jmp short loc_4C4403` (EB 06 90 90)
         *    so session flags (esi != 0) do not skip the BetSlot idle timer evaluation.
         * 3. 0x004C4440: NOP out `jnz short loc_4C4449` (75 07 -> 90 90) so credit balance does not abort timeout.
         */
        patch_memory(ADDR_BETSLOT_TIMEOUT, (const uint8_t *) &timeout_ms, sizeof(timeout_ms));

        static const uint8_t jmp_betslot_skip[4] = { 0xEB, 0x06, 0x90, 0x90 };
        patch_memory(ADDR_BETSLOT_SKIP_JUMP, jmp_betslot_skip, sizeof(jmp_betslot_skip));

        static const uint8_t nops2[2] = { 0x90, 0x90 };
        patch_memory(ADDR_BETSLOT_CREDIT_BYPASS, nops2, sizeof(nops2));

        log_info(
            "Patched Mode 11 (BetSlot) idle timeout (%u ms at 0x%08X), skip bypass (0x%08X), credit bypass (0x%08X)",
            timeout_ms,
            ADDR_BETSLOT_TIMEOUT,
            ADDR_BETSLOT_SKIP_JUMP,
            ADDR_BETSLOT_CREDIT_BYPASS);
    }

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

        /* In sub_4F2BC0 (0x004F2BC0, hardware self-test / initialization completion):
         * 0x004F2DC7: B9 05 00 00 00 (mov ecx, 5; call sub_425F70) for Station 0
         * 0x004F2DE7: B9 05 00 00 00 (mov ecx, 5; call sub_425F70) for Station 1
         * We patch the immediate byte at 0x004F2DC8 and 0x004F2DE8 to 0x00 so it executes
         * mov ecx, 0; call sub_425F70, eliminating the instant 1-frame flash of Slot Mode on boot!
         */
        static const uint8_t mode_zero = 0x00;
        patch_memory(ADDR_HARDWARE_TEST_MODE_0, &mode_zero, sizeof(mode_zero));
        patch_memory(ADDR_HARDWARE_TEST_MODE_1, &mode_zero, sizeof(mode_zero));

        log_info(
            "Patched CGameStatus::Init (0x%08X), sub_426370 (0x%08X), and sub_4F2BC0 (0x%08X, 0x%08X) to boot cleanly into Mode 0 (Title / Attract OP)",
            ADDR_BOOT_STATUS_INIT_MODE,
            ADDR_STARTUP_MODE_SWITCH_JUMP,
            ADDR_HARDWARE_TEST_MODE_0,
            ADDR_HARDWARE_TEST_MODE_1);
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


    init_raw_serial(cfg);
    s_anim_start_tick = GetTickCount();

    switch (cfg->cabinet_girl) {
        case KPMHOOK_CABINET_GIRL_RINKO:
            s_cabinet_jumper_byte = 0x01; /* Bit 0: Rinko (Kanojyo ID 1) */
            s_heroine_color = (struct rgb_val) { 50, 205, 50 }; /* Official Rinko Lime-Green (#78BB00) */
            log_info("PCSub cabinet hardware jumper: RINKO (Jumper=0x01, GirlID=1)");
            break;
        case KPMHOOK_CABINET_GIRL_NENE:
            s_cabinet_jumper_byte = 0x04; /* Bit 2: Nene (Kanojyo ID 2) */
            s_heroine_color = (struct rgb_val) { 255, 105, 180 }; /* Official Nene Magenta-Pink (#E4007F) */
            log_info("PCSub cabinet hardware jumper: NENE (Jumper=0x04, GirlID=2)");
            break;
        case KPMHOOK_CABINET_GIRL_MANAKA:
        default:
            s_cabinet_jumper_byte = 0x02; /* Bit 1: Manaka (Kanojyo ID 0) */
            s_heroine_color = (struct rgb_val) { 30, 144, 255 }; /* Official Manaka Sky Blue (#00A0E9) */
            log_info("PCSub cabinet hardware jumper: MANAKA (Jumper=0x02, GirlID=0)");
            break;
    }

    s_io_initialized = true;
    log_info("PCSub arcade I/O and lighting subsystem initialized successfully");
}

static void set_zone_color(int start_led, int count, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < count && (start_led + i) < TOTAL_LEDS; i++) {
        s_led_buffer[start_led + i].r = r;
        s_led_buffer[start_led + i].g = g;
        s_led_buffer[start_led + i].b = b;
    }
}

static void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    set_zone_color(0, TOTAL_LEDS, r, g, b);
}

static void handle_illumination_code(uint32_t code, uint32_t p1, uint32_t p2)
{
    s_current_anim_code = code;
    s_anim_start_tick = GetTickCount();

    /* Extract heroine custom color if supplied in payload (e.g. Code 16 in sub_45EAE0) */
    if (code == 16 && (p2 & 0xFFFFFF) != 0) {
        s_heroine_color.b = (uint8_t) (p2 & 0xFF);
        s_heroine_color.g = (uint8_t) ((p2 >> 4) & 0xFF);
        s_heroine_color.r = (uint8_t) ((p2 >> 8) & 0xFF);
    }

    log_info("Cabinet illumination code changed: code=%u, p1=%u, p2=0x%08X", code, p1, p2);
}

static void update_led_animations(DWORD now)
{
    DWORD elapsed = now - s_anim_start_tick;
    float t = (float) (now % 2000) / 2000.0f; /* 2.0s period */

    switch (s_current_anim_code) {
        case 1:
        case 14: /* All Off / Reset */
            set_all_leds(0, 0, 0);
            break;

        case 2:
        case 3:
        case 4: /* Test / Menu solid white illumination */
            set_all_leds(200, 200, 220);
            break;

        case 8: { /* Mode 8: Manaka Base Lighting (sub_47CC70 when GirlID=0, Sky Blue theme) */
            float breath = 0.8f + 0.2f * sinf((float) (now % 3000) * (3.14159f / 1500.0f));
            set_zone_color(0, 30, (uint8_t) (30 * breath), (uint8_t) (144 * breath), (uint8_t) (255 * breath));
            set_zone_color(30, 20, (uint8_t) (180 * breath), (uint8_t) (220 * breath), (uint8_t) (255 * breath));
            set_zone_color(50, 30, (uint8_t) (20 * breath), (uint8_t) (100 * breath), (uint8_t) (200 * breath));
            set_zone_color(80, 30, (uint8_t) (20 * breath), (uint8_t) (100 * breath), (uint8_t) (200 * breath));
            break;
        }

        case 9: { /* Mode 9: Rinko Base Lighting (sub_47CC70 when GirlID=1, Lime Green theme) */
            float breath = 0.8f + 0.2f * sinf((float) (now % 3000) * (3.14159f / 1500.0f));
            set_zone_color(0, 30, (uint8_t) (50 * breath), (uint8_t) (205 * breath), (uint8_t) (50 * breath));
            set_zone_color(30, 20, (uint8_t) (200 * breath), (uint8_t) (255 * breath), (uint8_t) (180 * breath));
            set_zone_color(50, 30, (uint8_t) (30 * breath), (uint8_t) (150 * breath), (uint8_t) (30 * breath));
            set_zone_color(80, 30, (uint8_t) (30 * breath), (uint8_t) (150 * breath), (uint8_t) (30 * breath));
            break;
        }

        case 10: { /* Mode 10: Nene Base Lighting (sub_47CC70 when GirlID=2, Magenta Pink theme) */
            float breath = 0.8f + 0.2f * sinf((float) (now % 3000) * (3.14159f / 1500.0f));
            set_zone_color(0, 30, (uint8_t) (255 * breath), (uint8_t) (105 * breath), (uint8_t) (180 * breath));
            set_zone_color(30, 20, (uint8_t) (255 * breath), (uint8_t) (200 * breath), (uint8_t) (220 * breath));
            set_zone_color(50, 30, (uint8_t) (200 * breath), (uint8_t) (50 * breath), (uint8_t) (120 * breath));
            set_zone_color(80, 30, (uint8_t) (200 * breath), (uint8_t) (50 * breath), (uint8_t) (120 * breath));
            break;
        }

        case 15: { /* Attract / Title Loop (sub_4A27E0): Running Rainbow Wave across LEDs */
            for (int i = 0; i < TOTAL_LEDS; i++) {
                float hue = (float) ((now / 8 + i * 8) % 360);
                float rad = hue * 3.14159f / 180.0f;
                uint8_t r = (uint8_t) (127.0f + 127.0f * sinf(rad));
                uint8_t g = (uint8_t) (127.0f + 127.0f * sinf(rad + 2.094f));
                uint8_t b = (uint8_t) (127.0f + 127.0f * sinf(rad + 4.188f));
                s_led_buffer[i].r = r;
                s_led_buffer[i].g = g;
                s_led_buffer[i].b = b;
            }
            break;
        }

        case 35:
        case 36:
        case 47:
        case 48: { /* ATARI / Bonus / Fever Win Celebration (sub_49DE90/49DEE0): Rapid Sparkle & Strobe */
            bool strobe = ((now / 75) % 2) == 0;
            uint8_t intensity = strobe ? 255 : 40;
            /* Alternate gold / pink / cyan bursts */
            uint8_t phase = (now / 300) % 3;
            if (phase == 0) {
                set_all_leds(intensity, intensity, 0); /* Gold */
            } else if (phase == 1) {
                set_all_leds(intensity, 40, intensity); /* Magenta */
            } else {
                set_all_leds(0, intensity, intensity); /* Cyan */
            }
            break;
        }

        case 16:
        case 17:
        case 18: { /* Girlfriend Interactive Touch / Upper Screen IR: Heartbeat Pulse */
            float pulse = 0.5f + 0.5f * sinf((float) (now % 800) * (3.14159f / 400.0f));
            uint8_t r = (uint8_t) (s_heroine_color.r * (0.3f + 0.7f * pulse));
            uint8_t g = (uint8_t) (s_heroine_color.g * (0.3f + 0.7f * pulse));
            uint8_t b = (uint8_t) (s_heroine_color.b * (0.3f + 0.7f * pulse));
            /* Focus pulse on Screen LEDs (0..29) and Front Pillars (30..49) */
            set_zone_color(0, 30, r, g, b);
            set_zone_color(30, 20, r, g, b);
            set_zone_color(50, 30, (uint8_t) (r * 0.5f), (uint8_t) (g * 0.5f), (uint8_t) (b * 0.5f));
            set_zone_color(80, 30, (uint8_t) (r * 0.5f), (uint8_t) (g * 0.5f), (uint8_t) (b * 0.5f));
            break;
        }

        case 43:
        case 44:
        case 45:
        case 46: { /* Date Event Level 1..4 / Normal Slot Ambient (sub_47CC70): Warm Romance glow */
            float breath = 0.6f + 0.4f * sinf((float) (now % 1500) * (3.14159f / 750.0f));
            uint8_t r = (uint8_t) (255 * breath);
            uint8_t g = (uint8_t) (130 * breath);
            uint8_t b = (uint8_t) (160 * breath);
            set_all_leds(r, g, b);
            break;
        }

        default: { /* Fallback ambient */
            float breath = 0.8f + 0.2f * sinf((float) (now % 3000) * (3.14159f / 1500.0f));
            set_zone_color(0, 30, (uint8_t) (s_heroine_color.r * breath), (uint8_t) (s_heroine_color.g * breath), (uint8_t) (s_heroine_color.b * breath));
            set_zone_color(30, 20, (uint8_t) (150 * breath), (uint8_t) (150 * breath), (uint8_t) (150 * breath));
            set_zone_color(50, 30, (uint8_t) (100 * breath), (uint8_t) (100 * breath), (uint8_t) (100 * breath));
            set_zone_color(80, 30, (uint8_t) (100 * breath), (uint8_t) (100 * breath), (uint8_t) (100 * breath));
            break;
        }
    }
}

static void send_raw_serial_frame(void)
{
    if (s_raw_serial_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    uint8_t frame[2 + TOTAL_LEDS * 3 + 2];
    frame[0] = 0xAA;
    frame[1] = 0x55;
    memcpy(&frame[2], s_led_buffer, TOTAL_LEDS * 3);
    frame[2 + TOTAL_LEDS * 3] = 0x55;
    frame[3 + TOTAL_LEDS * 3] = 0xAA;

    DWORD written;
    WriteFile(s_raw_serial_handle, frame, sizeof(frame), &written, NULL);
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

    /* Optional: Auto-transfer inserted 100-yen coins to medals without pressing Transfer button */
    if (s_cfg.coin_auto_transfer) {
        uint32_t *p_data = (uint32_t *) 0x00E652D4;
        if (p_data && *p_data) {
            uint32_t coin_meter = *((uint32_t *) (*p_data + 9608));
            static DWORD s_last_transfer_toggle = 0;
            static bool s_transfer_state = false;

            if (coin_meter > 0) {
                DWORD now_transfer = GetTickCount();
                if (now_transfer - s_last_transfer_toggle >= 100) {
                    s_transfer_state = !s_transfer_state;
                    s_last_transfer_toggle = now_transfer;
                }
                if (s_transfer_state) {
                    btn_mask |= KPM_IO_BTN_TRANSFER;
                }
            } else {
                s_transfer_state = false;
            }
        }
    }

    *((uint32_t *) (subwrap_base + 336)) = btn_mask;

    /* Offset 946 (0x3B2): PCSub Cabinet Girlfriend Jumper bitmask
     * Bit 1 (0x02) = Manaka (Kanojyo ID 0)
     * Bit 0 (0x01) = Rinko  (Kanojyo ID 1)
     * Bit 2 (0x04) = Nene   (Kanojyo ID 2)
     */
    subwrap_base[946] = s_cabinet_jumper_byte;

    /* Keep subboard instance helper buffer in sync if active */
    uint32_t *p_subboard = (uint32_t *) ADDR_DWORD_1ACFCBC;
    uint8_t *subboard_base = (p_subboard && *p_subboard) ? (uint8_t *) (*p_subboard) : NULL;
    if (subboard_base) {
        subboard_base[616] = s_cabinet_jumper_byte;

        /* Virtualize PCSub DS2430A authentic dongles */
        if (subboard_base[101] != 0 || subboard_base[143] != 0) {
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

            log_info(
                "Injected authentic DS2430A dongles into PCSub: SoftPlug status=%d, NetPlug status=%d, PCBID=%s",
                subboard_base[101],
                subboard_base[143],
                (const char *) (subboard_base + 216));
        }
    }

    /* Inject attract mode setting (USE GAME MODE) and ensure AGING MODE is clear:
     * *(g_pStationCtx_0 + 27): 0=BOTH ALT, 1=BOTH PANEL, 2=BOTH SLOT, 3=PANEL ONLY, 4=SLOT ONLY.
     * *(g_pStationCtx_0 + 144): AGING MODE (must be 0 to avoid auto-play / auto-credits).
     */
    uint32_t *p_ctx0 = (uint32_t *) ADDR_STATION_CTX_0;
    if (p_ctx0 && *p_ctx0) {
        uint8_t *ctx0 = (uint8_t *) (*p_ctx0);
        ctx0[27] = (uint8_t) s_cfg.attract_mode;
        ctx0[144] = 0;
        ctx0[40] = 1; /* Station eamuse enable */
        ctx0[42] = 1;
    }
    uint32_t *p_ctx1 = (uint32_t *) ADDR_STATION_CTX_1;
    if (p_ctx1 && *p_ctx1) {
        uint8_t *ctx1 = (uint8_t *) (*p_ctx1);
        ctx1[144] = 0;
    }
    uint32_t *p_ctx2 = (uint32_t *) ADDR_STATION_CTX_2;
    if (p_ctx2 && *p_ctx2) {
        uint8_t *ctx2 = (uint8_t *) (*p_ctx2);
        ctx2[144] = 0;
    }

    /* Keep CEamuseControl online status synchronized */
    uint32_t *p_eamuse = (uint32_t *) ADDR_GLOBAL_EAMUSE_CONTROL;
    if (p_eamuse && *p_eamuse) {
        uint8_t *eam_base = (uint8_t *) (*p_eamuse);
        eam_base[101] = 1; /* Station eamuse enable */
        *((uint16_t *) (eam_base + 272)) = 1; /* network_status = 1 (ONLINE) */
        *((uint16_t *) (eam_base + 274)) = 2; /* service_status = 2 (AVAILABLE) */
        *((uint32_t *) (eam_base + 264)) = 0; /* error_code = 0 */
    }

    /* Ensure subboard ready flag (subwrap + 284) is active so sub_5C9C40 runs all state machines */
    *((uint16_t *) (subwrap_base + 284)) = 1;

    /* Feed hardware buttons to subboard raw input buffer (offset 4) */
    if (subboard_base) {
        *((uint32_t *) (subboard_base + 4)) = btn_mask;
    }

    /* Offset 476 (0x1DC): MEDAL IN pulse counter
     *   Directly adds playable Game Credits via sub_401610(count * medal_rate, 0).
     *   Also feeds Selector 1 hardware registers (632..634) for Test Mode verification.
     */
    uint16_t medals = kpm_io_get_medal_pulse();
    if (medals > 0) {
        *((uint16_t *) (subwrap_base + 476)) += medals;
        if (subboard_base) {
            subboard_base[632] = 1; /* In-pulse flag */
            subboard_base[633] += 1; /* Seq */
            *((uint16_t *) (subboard_base + 634)) = medals; /* Count */
            subboard_base[625] = 0; /* Blocker normal */
            *((uint32_t *) (subboard_base + 628)) += 1;
        }
        log_info("Dispatched %u medal pulse(s) to CPcSubWrap (offset 476)", medals);
    }

    /* Offset 508 (0x1FC): COIN IN pulse counter
     *   Increments 100-yen coin meter accounting display via sub_401610(count, 4).
     *   Also feeds Selector 2 hardware registers (664..666) for Test Mode verification.
     */
    uint16_t coins = kpm_io_get_coin_pulse();
    if (coins > 0) {
        *((uint16_t *) (subwrap_base + 508)) += coins;
        if (subboard_base) {
            subboard_base[664] = 1; /* In-pulse flag */
            subboard_base[665] += 1; /* Seq */
            *((uint16_t *) (subboard_base + 666)) = coins; /* Count */
            subboard_base[659] = 0; /* Blocker normal */
            *((uint32_t *) (subboard_base + 660)) += 1;
        }
        log_info("Dispatched %u coin pulse(s) to CPcSubWrap (offset 508)", coins);
    }

    /* If player operates buttons, coins, or medals, reset the Slot Mode idle timer */
    if (btn_mask != 0 || medals > 0 || coins > 0) {
        uint32_t *p_idle_timer = get_slot_idle_timer();
        if (p_idle_timer) {
            *p_idle_timer = GetTickCount();
        }
    }

    /* --- Virtual Hopper Engine (出币/退币料斗仿真引擎闭环) ---
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
    uint16_t hopper_state = *((uint16_t *) (subwrap_base + 420));
    uint16_t req_payout = *((uint16_t *) (subwrap_base + 422));

    /* Capture new payout requests from game logic or test menu */
    if (hopper_state == 0 || hopper_state == 10 || hopper_state == 20) {
        if (req_payout > 0) {
            kpm_io_payout_demand(req_payout);
        }
    }

    /* State 20 (WaitAck): simulate subboard acknowledging error check command 288 */
    if (hopper_state == 20 && subboard_base) {
        *((uint16_t *) (subboard_base + 708)) = 0; /* No error */
        *((uint32_t *) (subboard_base + 712)) = *((uint32_t *) (subwrap_base + 436)) + 1;
    }

    /* Synchronize virtual hopper physical registers (motor, optical sensor, counters) */
    if (subboard_base) {
        uint16_t paid_count = 0;
        bool motor_running = false;
        bool sensor_active = false;
        uint8_t h_status = kpm_io_get_payout_detail(&paid_count, &motor_running, &sensor_active);

        /* Physical register emulation for Test Mode and Runtime */
        subboard_base[700] = motor_running ? 1 : 0;
        subboard_base[701] = sensor_active ? 1 : 0;
        subboard_base[702] = 0; /* 0: NORMAL */
        *((uint32_t *) (subboard_base + 704)) += 1;

        if (hopper_state == 50) {
            uint32_t *p_idle_timer = get_slot_idle_timer();
            if (p_idle_timer) {
                *p_idle_timer = GetTickCount(); /* Prevent idle timeout during large payouts */
            }

            if (h_status == 1 || h_status == 2) {
                subboard_base[691] = h_status;
                *((uint16_t *) (subboard_base + 692)) = paid_count;
                *((uint32_t *) (subboard_base + 696)) = *((uint32_t *) (subwrap_base + 432)) + 1;
            }
        }
    }

    if (hopper_state == 0) {
        /* Reset virtual hopper when game returns to idle */
        kpm_io_payout_demand(0);
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
        handle_illumination_code(code, p1, p2);
    }
    *((uint16_t *) (subwrap_base + 530)) = head;

    /* Update dynamic LED illumination patterns */
    DWORD now = GetTickCount();
    update_led_animations(now);

    /* Direct hardware discrete lamps: Offsets 356..366 (11 discrete channels) */
    uint32_t lamp_bits = 0;
    for (int i = 0; i < 11; i++) {
        uint8_t state = subwrap_base[356 + i];
        if (state != 2 && state != 0) {
            lamp_bits |= (1 << i);
        }
    }

    /* Normalized zone RGB calculations (Channels 11..22) */
    if (s_cfg.lights_normalized) {
        uint32_t r_sum, g_sum, b_sum;

        /* Screen Zone: LEDs 0..29 */
        r_sum = g_sum = b_sum = 0;
        for (int i = 0; i < 30; i++) {
            r_sum += s_led_buffer[i].r;
            g_sum += s_led_buffer[i].g;
            b_sum += s_led_buffer[i].b;
        }
        if ((r_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SCREEN_R;
        if ((g_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SCREEN_G;
        if ((b_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SCREEN_B;

        /* Front Zone: LEDs 30..49 */
        r_sum = g_sum = b_sum = 0;
        for (int i = 30; i < 50; i++) {
            r_sum += s_led_buffer[i].r;
            g_sum += s_led_buffer[i].g;
            b_sum += s_led_buffer[i].b;
        }
        if ((r_sum / 20) > 40) lamp_bits |= KPM_IO_LAMP_FRONT_R;
        if ((g_sum / 20) > 40) lamp_bits |= KPM_IO_LAMP_FRONT_G;
        if ((b_sum / 20) > 40) lamp_bits |= KPM_IO_LAMP_FRONT_B;

        /* Side Left Zone: LEDs 50..79 */
        r_sum = g_sum = b_sum = 0;
        for (int i = 50; i < 80; i++) {
            r_sum += s_led_buffer[i].r;
            g_sum += s_led_buffer[i].g;
            b_sum += s_led_buffer[i].b;
        }
        if ((r_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_L_R;
        if ((g_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_L_G;
        if ((b_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_L_B;

        /* Side Right Zone: LEDs 80..109 */
        r_sum = g_sum = b_sum = 0;
        for (int i = 80; i < 110; i++) {
            r_sum += s_led_buffer[i].r;
            g_sum += s_led_buffer[i].g;
            b_sum += s_led_buffer[i].b;
        }
        if ((r_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_R_R;
        if ((g_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_R_G;
        if ((b_sum / 30) > 40) lamp_bits |= KPM_IO_LAMP_SIDE_R_B;
    }

    static uint32_t s_last_logged_lamp_bits = 0xFFFFFFFF;
    if (lamp_bits != s_last_logged_lamp_bits) {
        log_info(
            "Lamp bits changed: 0x%08X (Btns: 0x%02X, POP: 0x%02X, Zones: 0x%03X)",
            lamp_bits,
            lamp_bits & 0x1F,
            (lamp_bits >> 5) & 0x3F,
            (lamp_bits >> 11) & 0xFFF);
        s_last_logged_lamp_bits = lamp_bits;
    }

    kpm_io_set_lamps(lamp_bits);

    /* Push raw 110-LED frame to DIY serial port if enabled */
    if (s_cfg.lights_raw_serial) {
        send_raw_serial_frame();
    }

    /* Periodically persist battery-backed SRAM NVRAM if modified */
    if (now - s_last_nvram_sync_tick >= 2000) {
        s_last_nvram_sync_tick = now;
        kpm_nvram_flush();
    }
}

void kpm_io_hook_fini(void)
{
    log_info("Flushing battery-backed SRAM NVRAM and finalizing I/O subsystems...");
    kpm_nvram_flush();

    if (s_raw_serial_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(s_raw_serial_handle);
        s_raw_serial_handle = INVALID_HANDLE_VALUE;
    }
}


