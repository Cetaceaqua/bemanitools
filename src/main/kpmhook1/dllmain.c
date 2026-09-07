#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include "kpmhook1/config-kpm.h"
#include "kpmhook1/d3d9-hook.h"
#include "kpmhook1/gfx-patch.h"
#include "kpmhook1/path-hook.h"
#include "kpmhook1/sound-hook.h"
#include "kpmhook1/touch-hook.h"
#include "kpmhook1/window-hook.h"
#include "util/defs.h"
#include "util/log.h"

#define KPMHOOK1_INFO_HEADER \
    "kpmhook1 for LovePlus MEDAL Happy Daily Life" \
    ", build " __DATE__ " " __TIME__

static void crash_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    HANDLE h = CreateFileA("crash.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, buf, len, &written, NULL);
        CloseHandle(h);
    }
}

static LONG WINAPI kpm_exception_filter(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        crash_log("CRASH ACCESS_VIOLATION at EIP=0x%08lx: %s address 0x%08lx\r\n",
                  ep->ContextRecord->Eip,
                  ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write to" :
                  ep->ExceptionRecord->ExceptionInformation[0] == 8 ? "DEP execute at" : "read from",
                  ep->ExceptionRecord->ExceptionInformation[1]);
        crash_log("Registers: EAX=0x%08lx EBX=0x%08lx ECX=0x%08lx EDX=0x%08lx ESI=0x%08lx EDI=0x%08lx EBP=0x%08lx ESP=0x%08lx\r\n",
                  ep->ContextRecord->Eax, ep->ContextRecord->Ebx, ep->ContextRecord->Ecx, ep->ContextRecord->Edx,
                  ep->ContextRecord->Esi, ep->ContextRecord->Edi, ep->ContextRecord->Ebp, ep->ContextRecord->Esp);
        uint32_t *stack = (uint32_t *) ep->ContextRecord->Esp;
        if (stack) {
            crash_log("Stack: [0]=0x%08lx [1]=0x%08lx [2]=0x%08lx [3]=0x%08lx [4]=0x%08lx [5]=0x%08lx [6]=0x%08lx [7]=0x%08lx\r\n",
                      stack[0], stack[1], stack[2], stack[3], stack[4], stack[5], stack[6], stack[7]);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static FILE *s_log_file = NULL;

static void kpm_composite_log_writer(void *ctx, const char *chars, size_t nchars)
{
    OutputDebugStringA(chars);
    if (s_log_file) {
        fwrite(chars, 1, nchars, s_log_file);
        fflush(s_log_file);
    }
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void *ctx)
{
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(mod);
    s_log_file = fopen("kpmhook1.log", "w");
    log_to_writer(kpm_composite_log_writer, NULL);
    AddVectoredExceptionHandler(1, kpm_exception_filter);

    log_info("=============================================================");
    log_info(KPMHOOK1_INFO_HEADER);
    log_info("Initializing kpmhook1 early process hooks...");
    log_info("=============================================================");

    if (!kpm_gfx_patch_verify()) {
        log_fatal("Target binary verification failed! Aborting injection.");
        return FALSE;
    }

    /* Intercept Direct3D9 to fix Windows 10/11 presentation incompatibilities */
    kpm_d3d9_hook_init(true);

    /* Force Direct3D9 windowed mode (disable fullscreen flags) */
    kpm_gfx_patch_apply(true);

    /* Redirect hardcoded D:/KPM/ and E:/ paths */
    kpm_path_hook_init();

    /* Hook Elo touchscreen library functions to prevent null pointer crash */
    kpm_touch_hook_init();

    /* Hook sound library loaders to prevent null pointer crash */
    kpm_sound_hook_init();

    /* Hook window creation, positioning, and dragging for dual-screen windowed mode */
    kpm_window_hook_init();

    /* Bootstrap compatibility game.conf if absent */
    kpm_config_bootstrap();

    log_info("kpmhook1 initialized successfully. Resuming game execution.");
    return TRUE;
}

