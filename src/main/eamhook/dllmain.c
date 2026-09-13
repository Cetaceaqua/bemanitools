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

static void __cdecl hooked_CLogEamuse_Put(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
        buf[--len] = '\0';
    }
    if (len > 0) {
        log_info("eam_if: %s", buf);
    }
}

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

    log_info("=============================================================");
    log_info(EAMHOOK_INFO_HEADER);
    log_info("Initializing eamhook for eam_if.exe...");
    log_info("=============================================================");

    struct cconfig *config = cconfig_init();
    struct eamhook_config cfg;

    eamhook_config_init(config);

    if (!cconfig_main_config_init(
            config,
            "--config",
            "eamhook.conf",
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

    /* Disable WMI network adapter check in eam3util to avoid CoInitializeSecurity
       failures and domain mismatches on modern Windows, forcing the built-in
       dummy adapter (domain: konami) to enable online mode */
    HMODULE eam3util = GetModuleHandleA("eam3util.dll");
    if (eam3util) {
        typedef void (*set_no_wmi_mode_t)(void);
        set_no_wmi_mode_t set_no_wmi = (set_no_wmi_mode_t) GetProcAddress(
            eam3util, "?SetNoWMIMode@CEamuseUtil@@SAXXZ");
        if (set_no_wmi) {
            set_no_wmi();
            log_info("Invoked CEamuseUtil::SetNoWMIMode() (bypassing WMI and domain check)");
        } else {
            log_warning("Could not find ?SetNoWMIMode@CEamuseUtil@@SAXXZ in eam3util.dll");
        }

        /* Hook CLogEamuse::Put to forward internal e-AMUSEMENT logs to eamhook.log */
        void *p_put = (void *) GetProcAddress(eam3util, "?Put@CLogEamuse@@SAXPBDZZ");
        if (p_put) {
            DWORD old_prot;
            if (VirtualProtect(p_put, 5, PAGE_EXECUTE_READWRITE, &old_prot)) {
                uint8_t *code = (uint8_t *) p_put;
                code[0] = 0xE9; /* jmp rel32 */
                int32_t rel = (int32_t) ((uint8_t *) hooked_CLogEamuse_Put - (code + 5));
                memcpy(&code[1], &rel, 4);
                VirtualProtect(p_put, 5, old_prot, &old_prot);
                log_info("Installed CLogEamuse::Put hook to capture eam_if diagnostics");
            }
        }
    }

    log_info("eamhook initialized successfully. Resuming eam_if execution.");
    return TRUE;
}
