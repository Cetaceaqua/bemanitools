#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#include "cconfig/cconfig-hook.h"
#include "kpmhook/config-gfx.h"
#include "kpmhook/config-io.h"
#include "kpmhook/config-kpm.h"
#include "kpmhook/d3d9-hook.h"
#include "kpmhook/gfx-patch.h"
#include "kpmhook/locale-hook.h"
#include "kpmhook/path-hook.h"
#include "kpmhook/sound-hook.h"
#include "kpmhook/touch-hook.h"
#include "kpmhook/window-hook.h"
#include "kpmhook/io-hook.h"
#include "kpmhook/reader-hook.h"
#include "kpmhook/movie-hook.h"
#include "util/defs.h"

#include "util/log.h"

#define KPMHOOK_INFO_HEADER \
    "kpmhook for LovePlus MEDAL Happy Daily Life" \
    ", build " __DATE__ " " __TIME__
#define KPMHOOK_CMD_USAGE \
    "Usage: inject.exe kpmhook.dll KT_SKELETON_ST_DUAL.EXE [options...]"

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
    if (reason == DLL_PROCESS_DETACH) {
        kpm_io_hook_fini();
        return TRUE;
    }

    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(mod);
    s_log_file = fopen("kpmhook.log", "w");
    log_to_writer(kpm_composite_log_writer, NULL);

    log_info("=============================================================");
    log_info(KPMHOOK_INFO_HEADER);
    log_info("Initializing kpmhook early process hooks...");
    log_info("=============================================================");

    struct cconfig *config = cconfig_init();
    struct kpmhook_config_gfx config_gfx;
    struct kpmhook_config_io config_io;

    kpmhook_config_gfx_init(config);
    kpmhook_config_io_init(config);

    if (!cconfig_hook_config_init(
            config,
            KPMHOOK_INFO_HEADER "\n" KPMHOOK_CMD_USAGE,
            CCONFIG_CMD_USAGE_OUT_DBG)) {
        cconfig_finit(config);
        log_fatal("cconfig initialization failed");
        return FALSE;
    }

    kpmhook_config_gfx_get(&config_gfx, config);
    kpmhook_config_io_get(&config_io, config);
    cconfig_finit(config);

    if (!kpm_gfx_patch_verify()) {
        log_fatal("Target binary verification failed! Aborting injection.");
        return FALSE;
    }

    /* Bootstrap compatibility game.conf if absent */
    kpm_config_bootstrap(&config_gfx);

    /* Intercept Direct3D9 to fix Windows 10/11 presentation incompatibilities */
    kpm_d3d9_hook_init(config_gfx.windowed);

    /* Force Direct3D9 windowed mode (disable fullscreen flags) */
    kpm_gfx_patch_apply(config_gfx.windowed);

    /* Redirect hardcoded D:/KPM/ and E:/ paths */
    kpm_path_hook_init();

    /* Hook Elo touchscreen library functions to prevent null pointer crash */
    kpm_touch_hook_init();

    /* Hook sound library loaders to prevent null pointer crash */
    kpm_sound_hook_init();

    /* Hook window creation, positioning, and dragging for dual-screen windowed mode */
    kpm_window_hook_init();

    /* Redirect ANSI string conversions to Shift-JIS (CP932) and fix Japanese fonts */
    kpm_locale_hook_init();

    /* Hook PCSub arcade I/O, cabinet lights, and patch button/medal polling */
    kpm_io_hook_init(&config_io);

    /* Hook COM1 card reader (eamio virtual reader or physical serial passthrough) */
    kpm_reader_hook_init(&config_io);

    /* Hook DirectShow VMR9 movie playback, COM apartment, and Step 17 watchdog */
    kpm_movie_hook_init(&config_io);

    log_info("kpmhook initialized successfully. Resuming game execution.");

    return TRUE;
}
