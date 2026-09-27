#define LOG_MODULE "kpm-conf"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kpmhook/config-kpm.h"
#include "util/log.h"

static int s_rotate = 1;
static int s_sub_screen = 1;
static int s_upper_screen_touch = 1;

void kpm_config_bootstrap(
    const struct kpmhook_config_gfx *gfx_cfg,
    const struct kpmhook_config_io *io_cfg)
{
    char root[MAX_PATH];
    char conf_path[MAX_PATH];
    char *last_sep;

    if (gfx_cfg) {
        s_rotate = gfx_cfg->rotate ? 1 : 0;
        s_sub_screen = gfx_cfg->sub_screen ? 1 : 0;
    }

    if (io_cfg) {
        s_upper_screen_touch = io_cfg->upper_screen_touch ? 1 : 0;
    }

    GetModuleFileNameA(NULL, root, sizeof(root));
    last_sep = strrchr(root, '\\');
    if (!last_sep) {
        last_sep = strrchr(root, '/');
    }
    if (last_sep) {
        *last_sep = '\0';
    }

    _snprintf(conf_path, sizeof(conf_path), "%s\\game.conf", root);

    if (GetFileAttributesA(conf_path) != INVALID_FILE_ATTRIBUTES) {
        log_info("Found existing custom game.conf at %s", conf_path);
    } else {
        log_info(
            "game.conf not found; running with authentic arcade internal defaults "
            "(NO_SUBBOARD=0, NO_TOUCHPANEL=0, dual screen 1024x768)");
    }
}

bool kpm_config_get_rotate(void)
{
    return s_rotate != 0;
}

bool kpm_config_get_sub_screen(void)
{
    return s_sub_screen != 0;
}

bool kpm_config_get_upper_screen_touch(void)
{
    return s_upper_screen_touch != 0;
}
