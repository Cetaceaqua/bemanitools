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
static struct rgb_val s_heroine_color = { 255, 105, 180 }; /* Default Manaka Pink */

static void patch_memory(uintptr_t addr, const uint8_t *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
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

void kpm_io_hook_init(const struct kpmhook_config_io *cfg)
{
    log_info("Initializing PCSub arcade I/O virtualization...");
    s_cfg = *cfg;

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

    init_raw_serial(cfg);
    s_anim_start_tick = GetTickCount();

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
        case 1: /* All Off */
            set_all_leds(0, 0, 0);
            break;

        case 2:
        case 3:
        case 4: /* Test / Menu solid white or illumination */
            set_all_leds(200, 200, 220);
            break;

        case 9: { /* Attract / Title Loop: Running Rainbow Wave across side and screen LEDs */
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

        case 10:
        case 35:
        case 36:
        case 47:
        case 48: { /* ATARI / Bonus / Fever Win Celebration: Rapid Sparkle & Strobe */
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
        case 46: { /* Date Event Level 1..4: Warm Romance glow */
            float breath = 0.6f + 0.4f * sinf((float) (now % 1500) * (3.14159f / 750.0f));
            uint8_t r = (uint8_t) (255 * breath);
            uint8_t g = (uint8_t) (130 * breath);
            uint8_t b = (uint8_t) (160 * breath);
            set_all_leds(r, g, b);
            break;
        }

        case 8:
        default: { /* Normal Slot Play: Warm amber / soft romantic ambient backlight */
            float breath = 0.8f + 0.2f * sinf((float) (now % 3000) * (3.14159f / 1500.0f));
            /* Screen: Warm pastel pink/amber */
            set_zone_color(0, 30, (uint8_t) (200 * breath), (uint8_t) (120 * breath), (uint8_t) (140 * breath));
            /* Front: Gentle white */
            set_zone_color(30, 20, (uint8_t) (150 * breath), (uint8_t) (150 * breath), (uint8_t) (150 * breath));
            /* Sides: Warm soft cyan/gold */
            set_zone_color(50, 30, (uint8_t) (120 * breath), (uint8_t) (100 * breath), (uint8_t) (160 * breath));
            set_zone_color(80, 30, (uint8_t) (120 * breath), (uint8_t) (100 * breath), (uint8_t) (160 * breath));
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
    *((uint32_t *) (subwrap_base + 336)) = (uint32_t) btns;

    /* Offset 508 (0x1FC): Medal In pulse counter */
    uint16_t coins = kpm_io_get_coin_pulse();
    if (coins > 0) {
        *((uint16_t *) (subwrap_base + 508)) += coins;
        log_info("Dispatched %u medal pulses to CPcSubWrap", coins);
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

    kpm_io_set_lamps(lamp_bits);

    /* Push raw 110-LED frame to DIY serial port if enabled */
    if (s_cfg.lights_raw_serial) {
        send_raw_serial_frame();
    }
}

