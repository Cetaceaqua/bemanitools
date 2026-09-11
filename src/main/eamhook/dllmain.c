#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include "cconfig/cconfig-hook.h"
#include "cconfig/cconfig-main.h"
#include "eamhook/config.h"
#include "eamhook/eamuse.h"
#include "eamhook/path.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

#define EAMHOOK_INFO_HEADER \
    "eamhook for LovePlus MEDAL eam_if broker" \
    ", build " __DATE__ " " __TIME__
#define EAMHOOK_CMD_USAGE \
    "Usage: inject.exe eamhook.dll eam_if.exe [options...]"

static FILE *s_log_file = NULL;

static void eamhook_log_writer(void *ctx, const char *chars, size_t nchars)
{
    OutputDebugStringA(chars);
    if (s_log_file) {
        fwrite(chars, 1, nchars, s_log_file);
        fflush(s_log_file);
    }
}

static void crash_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    HANDLE h = CreateFileA("eam_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, buf, len, &written, NULL);
        CloseHandle(h);
    }
}

static LONG WINAPI eam_exception_filter(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        crash_log("EAM CRASH ACCESS_VIOLATION at EIP=0x%08lx: %s address 0x%08lx\r\n",
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

static void (WINAPI *real_ExitProcess)(UINT);
static BOOL (WINAPI *real_TerminateProcess)(HANDLE, UINT);

static void WINAPI my_ExitProcess(UINT uExitCode)
{
    void *callers[8];
    USHORT n = CaptureStackBackTrace(0, 8, callers, NULL);
    crash_log("ExitProcess(%u) called! Backtrace:\r\n", uExitCode);
    for (USHORT i = 0; i < n; i++) {
        crash_log("  [%u] 0x%p\r\n", i, callers[i]);
    }
    real_ExitProcess(uExitCode);
}

static BOOL WINAPI my_TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    void *callers[8];
    USHORT n = CaptureStackBackTrace(0, 8, callers, NULL);
    crash_log("TerminateProcess(%u) called! Backtrace:\r\n", uExitCode);
    for (USHORT i = 0; i < n; i++) {
        crash_log("  [%u] 0x%p\r\n", i, callers[i]);
    }
    return real_TerminateProcess(hProcess, uExitCode);
}

static const struct hook_symbol eam_exit_syms[] = {
    { .name = "ExitProcess",      .patch = my_ExitProcess,      .link = (void **) &real_ExitProcess },
    { .name = "TerminateProcess", .patch = my_TerminateProcess, .link = (void **) &real_TerminateProcess },
};

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void *ctx)
{
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    /* Pin eamhook.dll in process memory so it can never be unloaded */
    HMODULE h_self;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        (LPCSTR) DllMain,
        &h_self);

    DisableThreadLibraryCalls(mod);
    s_log_file = fopen("eamhook.log", "w");
    log_to_writer(eamhook_log_writer, NULL);
    AddVectoredExceptionHandler(1, eam_exception_filter);

    log_info("=============================================================");
    log_info(EAMHOOK_INFO_HEADER);
    log_info("Initializing eamhook for eam_if.exe...");
    log_info("=============================================================");

    struct cconfig *config = cconfig_init();
    struct eamhook_config cfg;

    eamhook_config_init(config);

    const char *def_conf = NULL;
    char conf_path[MAX_PATH];

    /* Check 1: contents\eamhook.conf (next to eamhook.dll) */
    GetModuleFileNameA(h_self, conf_path, sizeof(conf_path));
    char *p = strrchr(conf_path, '\\');
    if (!p) p = strrchr(conf_path, '/');
    if (p) {
        *(p + 1) = '\0';
        strncat(conf_path, "eamhook.conf", sizeof(conf_path) - strlen(conf_path) - 1);
        if (GetFileAttributesA(conf_path) != INVALID_FILE_ATTRIBUTES) {
            def_conf = conf_path;
        }
    }

    /* Check 2: current directory eamhook.conf */
    if (!def_conf && GetFileAttributesA("eamhook.conf") != INVALID_FILE_ATTRIBUTES) {
        def_conf = "eamhook.conf";
    }

    /* Check 3: eamuse\eamhook.conf */
    if (!def_conf && GetFileAttributesA("eamuse\\eamhook.conf") != INVALID_FILE_ATTRIBUTES) {
        def_conf = "eamuse\\eamhook.conf";
    }

    if (!cconfig_main_config_init(
            config,
            "--config",
            def_conf,
            "--help",
            "-h",
            EAMHOOK_INFO_HEADER "\n" EAMHOOK_CMD_USAGE,
            CCONFIG_CMD_USAGE_OUT_DBG)) {
        cconfig_finit(config);
        log_fatal("cconfig initialization failed");
        return FALSE;
    }

    eamhook_config_get(&cfg, config);
    cconfig_finit(config);

    /* Hook path operations (redirect E:\ to .\e\) */
    eamhook_path_init();

    /* Hook ws2_32.dll for E-Amusement redirect */
    eamuse_hook_init();
    eamuse_set_addr(&cfg.server_addr);

    /* Hook ExitProcess/TerminateProcess to trace exit reasons */
    hook_table_apply(
        NULL, "kernel32.dll", eam_exit_syms, lengthof(eam_exit_syms));

    log_info("eamhook initialized successfully. Resuming eam_if execution.");
    return TRUE;
}
