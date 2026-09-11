#define LOG_MODULE "kpm-io-hook"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "bemanitools/kpmio.h"
#include "kpmhook/io-hook.h"
#include "util/log.h"

#define ADDR_KEYBOARD_LOOP_START  0x00411686
#define ADDR_KEYBOARD_LOOP_END    0x00411C56
#define ADDR_SUBBOARD_CHECK_JUMP  0x00411CC4
#define ADDR_DWORD_1ACFCB8        0x01ACFCB8

static bool s_io_initialized = false;

static void patch_memory(uintptr_t addr, const uint8_t *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
}

void kpm_io_hook_init(bool disable_debug_keys)
{
    log_info("Initializing PCSub arcade I/O virtualization...");

    /* Initialize kpmio backend */
    kpm_io_set_loggers(
        log_impl_misc,
        log_impl_info,
        log_impl_warning,
        log_impl_fatal);

    if (!kpm_io_init(NULL, NULL, NULL)) {
        log_warning("kpm_io_init returned false; falling back to direct emulation");
    }

    if (disable_debug_keys) {
        /* In sub_411660:
         * 0x00411685: push ebp
         * 0x00411686: mov ebp, [esi]   (8B 2E)
         * 0x00411688: push 70h         (6A 70)  ; VK_F1
         * 0x0041168A: call edi         (FF D7)  ; GetAsyncKeyState
         * ... queries ~60 keys (F1..F10, 1..0, A..Z, Arrows, Numpad) ...
         * 0x00411C56: push 1Bh                  ; VK_ESCAPE
         * 0x00411C58: call edi                  ; GetAsyncKeyState(VK_ESCAPE)
         * 0x00411C5A: test ax, ax
         * 0x00411C5D: pop ebp
         * 0x00411C5E: jns short loc_411CA3
         *
         * Patching 0x00411686 with a jump to 0x00411C56 (relative offset: +0x5CB)
         * skips all developer debug keys, avoids any input collisions with kpmio,
         * preserves clean ESC exit handling, and leaves stack 100% balanced.
         */
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

    /* In sub_411660:
     * 0x00411CC1: cmp eax, 1
     * 0x00411CC4: jz loc_411DD2 (0F 84 08 01 00 00)
     * Patch the 6-byte conditional jump with NOPs so the game always reads
     * subboard hardware inputs (dword_1ACFCB8 + 0x150) even when NO_SUBBOARD=1.
     */
    static const uint8_t nops6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    patch_memory(ADDR_SUBBOARD_CHECK_JUMP, nops6, sizeof(nops6));
    log_info("Patched subboard check jump at 0x%08X to NOPs", ADDR_SUBBOARD_CHECK_JUMP);

    s_io_initialized = true;
    log_info("PCSub arcade I/O hook initialized successfully");
}

void kpm_io_hook_update(void)
{
    if (!s_io_initialized) {
        return;
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
    *((uint32_t *) (subwrap_base + 336)) = (uint32_t) btns;

    /* Offset 508 (0x1FC): Medal In pulse counter */
    uint16_t coins = kpm_io_get_coin_pulse();
    if (coins > 0) {
        *((uint16_t *) (subwrap_base + 508)) += coins;
        log_info("Dispatched %u medal pulses to CPcSubWrap", coins);
    }

    /* Offsets 356..366 (0x164..0x16E): Hardware Lamp states
     * 2 = OFF, 4 = ON, 1/3 = blinking/flashing. Any value != 2 and != 0 is active.
     */
    uint32_t lamp_bits = 0;
    for (int i = 0; i < 11; i++) {
        uint8_t state = subwrap_base[356 + i];
        if (state != 2 && state != 0) {
            lamp_bits |= (1 << i);
        }
    }
    kpm_io_set_lamps(lamp_bits);
}
