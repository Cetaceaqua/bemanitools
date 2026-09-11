#define LOG_MODULE "kpm-reader-hook"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bemanitools/eamio.h"
#include "bemanitools/glue.h"
#include "hook/table.h"
#include "kpmhook/config-io.h"
#include "kpmhook/reader-hook.h"
#include "util/log.h"
#include "util/thread.h"

#define VIRTUAL_READER_HANDLE ((HANDLE)(intptr_t) 0xCA4D0001)
#define RX_BUFFER_SIZE 4096

static struct kpmhook_config_io s_cfg;
static CRITICAL_SECTION s_reader_cs;
static uint8_t s_rx_buf[RX_BUFFER_SIZE];
static size_t s_rx_head = 0;
static size_t s_rx_tail = 0;
static bool s_is_open = false;
static bool s_eamio_inited = false;

/* Win32 real function pointers */
static HANDLE (WINAPI *real_CreateFileA)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL (WINAPI *real_GetCommProperties)(HANDLE, LPCOMMPROP);
static BOOL (WINAPI *real_GetCommState)(HANDLE, LPDCB);
static BOOL (WINAPI *real_SetCommState)(HANDLE, LPDCB);
static BOOL (WINAPI *real_SetCommTimeouts)(HANDLE, LPCOMMTIMEOUTS);
static BOOL (WINAPI *real_ClearCommError)(HANDLE, LPDWORD, LPCOMSTAT);
static BOOL (WINAPI *real_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL (WINAPI *real_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL (WINAPI *real_CloseHandle)(HANDLE);
static BOOL (WINAPI *real_CancelIo)(HANDLE);

static size_t ring_available(void)
{
    if (s_rx_head >= s_rx_tail) {
        return s_rx_head - s_rx_tail;
    } else {
        return (RX_BUFFER_SIZE - s_rx_tail) + s_rx_head;
    }
}

static void ring_write_byte(uint8_t b)
{
    size_t next_head = (s_rx_head + 1) % RX_BUFFER_SIZE;
    if (next_head != s_rx_tail) {
        s_rx_buf[s_rx_head] = b;
        s_rx_head = next_head;
    } else {
        log_warning("Reader RX ring buffer overflow!");
    }
}

static void ring_read(uint8_t *dest, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (s_rx_tail != s_rx_head) {
            dest[i] = s_rx_buf[s_rx_tail];
            s_rx_tail = (s_rx_tail + 1) % RX_BUFFER_SIZE;
        } else {
            dest[i] = 0;
        }
    }
}

static void queue_response(const uint8_t *data, size_t len)
{
    EnterCriticalSection(&s_reader_cs);
    if (len > 0) {
        /* Byte 0 is sync 0xAA, unescaped */
        ring_write_byte(data[0]);
        for (size_t i = 1; i < len; i++) {
            uint8_t b = data[i];
            if (b == 0xAA) {
                ring_write_byte(0xFF);
                ring_write_byte((uint8_t)(~0xAA));
            } else if (b == 0xFF) {
                ring_write_byte(0xFF);
                ring_write_byte((uint8_t)(~0xFF));
            } else {
                ring_write_byte(b);
            }
        }
    }
    LeaveCriticalSection(&s_reader_cs);
}

static void ensure_eamio_initialized(void)
{
    if (s_eamio_inited) {
        return;
    }
    s_eamio_inited = true;

    log_info("Initializing eamio for virtual card reader emulation...");
    eam_io_set_loggers(
        log_impl_misc,
        log_impl_info,
        log_impl_warning,
        log_impl_fatal);

    if (!eam_io_init(crt_thread_create, crt_thread_join, crt_thread_destroy)) {
        log_warning("eam_io_init returned false; card reader emulation may be inactive");
    } else {
        log_info("eamio initialized successfully");
    }
}

static void handle_reader_write(const uint8_t *cmd, DWORD len)
{
    if (!cmd || len == 0) {
        return;
    }

    /* Single byte 0xAA sync probe (State 1) */
    if (len == 1 && cmd[0] == 0xAA) {
        uint8_t resp = 0xAA;
        queue_response(&resp, 1);
        return;
    }

    if (cmd[0] != 0xAA || len < 4) {
        return;
    }

    uint8_t c_cmd = cmd[2];
    uint8_t c_subcmd = cmd[3];

    /* Card polling query: cmd=0x01, subcmd=0x34 (e.g. AA 00 01 34 01 10 46) */
    if (c_cmd == 0x01 && c_subcmd == 0x34) {
        static bool s_logged_first_poll = false;
        if (!s_logged_first_poll) {
            s_logged_first_poll = true;
            log_info("Game started card reader RFID polling loop (cmd 0x01:0x34)");
        }

        ensure_eamio_initialized();
        eam_io_poll(0);
        uint8_t sensor = eam_io_get_sensor_state(0);

        /* Automatic keyboard fallback: if eamio has no HID device configured in config.exe,
           check NumPad Plus (VK_ADD) and standard Plus/Equal (VK_OEM_PLUS) */
        if (!sensor) {
            if ((GetAsyncKeyState(VK_ADD) & 0x8000) || (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000)) {
                sensor = (1 << EAM_IO_SENSOR_FRONT) | (1 << EAM_IO_SENSOR_BACK);
            }
        }

        if (sensor != 0) {
            uint8_t uid[8];
            memset(uid, 0, sizeof(uid));
            uint8_t card_type = eam_io_read_card(0, uid, sizeof(uid));

            /* Fallback: if eam_io_read_card returned none (e.g. card_path not configured or failed),
               try reading card0.txt directly */
            if (card_type == EAM_IO_CARD_NONE) {
                FILE *cf = fopen("card0.txt", "r");
                if (!cf) {
                    cf = fopen("contents\\card0.txt", "r");
                }
                if (cf) {
                    char line[64];
                    if (fgets(line, sizeof(line), cf)) {
                        char *p = line;
                        while (*p && (*p == ' ' || *p == '\t')) p++;
                        char hex[17];
                        strncpy(hex, p, 16);
                        hex[16] = '\0';
                        for (int i = 0; i < 8; i++) {
                            unsigned int b = 0;
                            sscanf(&hex[i * 2], "%02X", &b);
                            uid[i] = (uint8_t) b;
                        }
                        card_type = (uid[0] == 0xE0 && uid[1] == 0x04) ? 
                            EAM_IO_CARD_ISO15696 : EAM_IO_CARD_FELICA;
                    }
                    fclose(cf);
                }
                if (card_type == EAM_IO_CARD_NONE) {
                    /* Hardcoded fallback card ID */
                    uid[0] = 0xE0; uid[1] = 0x04; uid[2] = 0x01; uid[3] = 0x00;
                    uid[4] = 0x23; uid[5] = 0x7C; uid[6] = 0x93; uid[7] = 0x16;
                    card_type = EAM_IO_CARD_ISO15696;
                }
            }

            /* 1 = ISO15693 (e-Amusement Pass), 0 = FeliCa */
            uint8_t kpm_card_type = (card_type == EAM_IO_CARD_FELICA) ? 0 : 1;

            uint8_t resp[18];
            resp[0] = 0xAA;
            resp[1] = 0x00;
            resp[2] = 0x01;
            resp[3] = 0x34;
            resp[4] = 0x0C; /* 12 payload bytes */
            resp[5] = 0x01; /* Status: card present (non-zero, != 4) */
            resp[6] = kpm_card_type;
            memcpy(&resp[7], uid, 8);
            resp[15] = 0x00;
            resp[16] = 0x00;

            uint8_t csum = 0;
            for (int i = 1; i <= 16; i++) {
                csum += resp[i];
            }
            resp[17] = csum;

            queue_response(resp, sizeof(resp));
            log_info(
                "Card detected! Type=%s UID=%02X%02X%02X%02X%02X%02X%02X%02X",
                kpm_card_type ? "ISO15693" : "FeliCa",
                uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);
        } else {
            /* No card present: idle response */
            uint8_t resp[7];
            resp[0] = 0xAA;
            resp[1] = 0x00;
            resp[2] = 0x01;
            resp[3] = 0x34;
            resp[4] = 0x01;
            resp[5] = 0x00; /* No card */
            resp[6] = (uint8_t)(resp[1] + resp[2] + resp[3] + resp[4] + resp[5]);
            queue_response(resp, sizeof(resp));
        }
        return;
    }

    /* Generic ACK for setup/handshake commands (states 2, 4, 6, 8, 10, 12, 14) */
    uint8_t resp[7];
    resp[0] = 0xAA;
    resp[1] = 0x00;
    resp[2] = c_cmd;
    resp[3] = c_subcmd;
    resp[4] = 0x01;
    resp[5] = 0x00; /* Status OK */
    resp[6] = (uint8_t)(resp[1] + resp[2] + resp[3] + resp[4] + resp[5]);
    queue_response(resp, sizeof(resp));
}

static bool is_com1(LPCSTR name)
{
    if (!name) return false;
    return (_stricmp(name, "COM1") == 0 ||
            _stricmp(name, "\\\\.\\COM1") == 0 ||
            _stricmp(name, "COM1:") == 0);
}

static HANDLE WINAPI my_CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    if (is_com1(lpFileName)) {
        if (s_cfg.card_port[0] != '\0') {
            char target[64];
            if (strncmp(s_cfg.card_port, "\\\\.\\", 4) == 0) {
                snprintf(target, sizeof(target), "%s", s_cfg.card_port);
            } else {
                snprintf(target, sizeof(target), "\\\\.\\%s", s_cfg.card_port);
            }
            HANDLE h = real_CreateFileA(
                target, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            log_info("Reader COM1 redirected to real serial port '%s' (h=%p)", target, h);
            return h;
        }

        log_info("Reader COM1 opened (using virtual eamio reader)");
        EnterCriticalSection(&s_reader_cs);
        s_is_open = true;
        s_rx_head = 0;
        s_rx_tail = 0;
        LeaveCriticalSection(&s_reader_cs);
        return VIRTUAL_READER_HANDLE;
    }

    return real_CreateFileA(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static BOOL WINAPI my_GetCommProperties(HANDLE hFile, LPCOMMPROP lpCommProp)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        if (lpCommProp) {
            memset(lpCommProp, 0, sizeof(COMMPROP));
            lpCommProp->wPacketLength = sizeof(COMMPROP);
            /* Include BAUD_9600 (0x800) required by sub_50AD60 */
            lpCommProp->dwSettableBaud = 0x800 | 0x1000 | 0x2000;
            lpCommProp->dwCurrentRxQueue = 1024;
            lpCommProp->dwCurrentTxQueue = 1024;
        }
        return TRUE;
    }
    return real_GetCommProperties(hFile, lpCommProp);
}

static BOOL WINAPI my_GetCommState(HANDLE hFile, LPDCB lpDCB)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        if (lpDCB) {
            memset(lpDCB, 0, sizeof(DCB));
            lpDCB->DCBlength = sizeof(DCB);
            lpDCB->BaudRate = CBR_9600;
            lpDCB->ByteSize = 8;
            lpDCB->Parity = NOPARITY;
            lpDCB->StopBits = ONESTOPBIT;
        }
        return TRUE;
    }
    return real_GetCommState(hFile, lpDCB);
}

static BOOL WINAPI my_SetCommState(HANDLE hFile, LPDCB lpDCB)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        return TRUE;
    }
    return real_SetCommState(hFile, lpDCB);
}

static BOOL WINAPI my_SetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        return TRUE;
    }
    return real_SetCommTimeouts(hFile, lpCommTimeouts);
}

static BOOL WINAPI my_ClearCommError(HANDLE hFile, LPDWORD lpErrors, LPCOMSTAT lpStat)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        if (lpErrors) {
            *lpErrors = 0;
        }
        if (lpStat) {
            memset(lpStat, 0, sizeof(COMSTAT));
            EnterCriticalSection(&s_reader_cs);
            lpStat->cbInQue = (DWORD) ring_available();
            LeaveCriticalSection(&s_reader_cs);
        }
        return TRUE;
    }
    return real_ClearCommError(hFile, lpErrors, lpStat);
}

static BOOL WINAPI my_ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        EnterCriticalSection(&s_reader_cs);
        DWORD avail = (DWORD) ring_available();
        DWORD to_read = nNumberOfBytesToRead;
        if (to_read > avail) {
            to_read = avail;
        }
        if (to_read > 0 && lpBuffer) {
            ring_read((uint8_t *) lpBuffer, to_read);
        }
        if (lpNumberOfBytesRead) {
            *lpNumberOfBytesRead = to_read;
        }
        LeaveCriticalSection(&s_reader_cs);
        return TRUE;
    }
    return real_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

static BOOL WINAPI my_WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        if (lpNumberOfBytesWritten) {
            *lpNumberOfBytesWritten = nNumberOfBytesToWrite;
        }
        handle_reader_write((const uint8_t *) lpBuffer, nNumberOfBytesToWrite);
        return TRUE;
    }
    return real_WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

static BOOL WINAPI my_CloseHandle(HANDLE hObject)
{
    if (hObject == VIRTUAL_READER_HANDLE) {
        log_info("Reader COM1 handle closed");
        EnterCriticalSection(&s_reader_cs);
        s_is_open = false;
        LeaveCriticalSection(&s_reader_cs);
        return TRUE;
    }
    return real_CloseHandle(hObject);
}

static BOOL WINAPI my_CancelIo(HANDLE hFile)
{
    if (hFile == VIRTUAL_READER_HANDLE) {
        return TRUE;
    }
    return real_CancelIo(hFile);
}

static const struct hook_symbol kpm_reader_syms[] = {
    { .name = "CreateFileA",        .patch = my_CreateFileA,        .link = (void **) &real_CreateFileA },
    { .name = "GetCommProperties",  .patch = my_GetCommProperties,  .link = (void **) &real_GetCommProperties },
    { .name = "GetCommState",       .patch = my_GetCommState,       .link = (void **) &real_GetCommState },
    { .name = "SetCommState",       .patch = my_SetCommState,       .link = (void **) &real_SetCommState },
    { .name = "SetCommTimeouts",    .patch = my_SetCommTimeouts,    .link = (void **) &real_SetCommTimeouts },
    { .name = "ClearCommError",     .patch = my_ClearCommError,     .link = (void **) &real_ClearCommError },
    { .name = "ReadFile",           .patch = my_ReadFile,           .link = (void **) &real_ReadFile },
    { .name = "WriteFile",          .patch = my_WriteFile,          .link = (void **) &real_WriteFile },
    { .name = "CloseHandle",        .patch = my_CloseHandle,        .link = (void **) &real_CloseHandle },
    { .name = "CancelIo",           .patch = my_CancelIo,           .link = (void **) &real_CancelIo },
};

void kpm_reader_hook_init(const struct kpmhook_config_io *cfg)
{
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    InitializeCriticalSection(&s_reader_cs);

    hook_table_apply(
        NULL,
        "kernel32.dll",
        kpm_reader_syms,
        sizeof(kpm_reader_syms) / sizeof(kpm_reader_syms[0]));

    log_info("Installed COM1 card reader hook (mode: %s)",
             s_cfg.card_port[0] ? s_cfg.card_port : "virtual eamio");
}

void kpm_reader_hook_fini(void)
{
    if (s_eamio_inited) {
        eam_io_fini();
        s_eamio_inited = false;
    }
    DeleteCriticalSection(&s_reader_cs);
}
