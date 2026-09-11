#define LOG_MODULE "kpm-conf"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kpmhook/config-kpm.h"
#include "util/log.h"

static int s_rotate = 1;

void kpm_config_bootstrap(const struct kpmhook_config_gfx *gfx_cfg)
{
    char root[MAX_PATH];
    char conf_path[MAX_PATH];
    char *last_sep;
    FILE *f_out;

    if (gfx_cfg) {
        s_rotate = gfx_cfg->rotate ? 1 : 0;
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

    /* If game.conf already exists, do nothing */
    if (GetFileAttributesA(conf_path) != INVALID_FILE_ATTRIBUTES) {
        log_info("Found existing game.conf at %s", conf_path);
        return;
    }

    log_info("game.conf not found. Generating minimal standalone game.conf...");

    f_out = fopen(conf_path, "wt");
    if (!f_out) {
        log_warning("Failed to create %s for writing", conf_path);
        return;
    }

    /* Minimal entries needed for standalone PC operation without arcade IO boards.
     * Unspecified entries will safely use the game's internal default values.
     */
    fputs("# LovePlus MEDAL (KPM) minimal configuration\n", f_out);
    fputs("NO_TOUCHPANEL=1\n", f_out);
    fputs("NO_SUBBOARD=1\n", f_out);
    fputs("NO_CARDREADER=0\n", f_out);
    fputs("IGNORE_IRCOM=1\n", f_out);
    fputs("DISABLE_ALL_ERROR=1\n", f_out);
    fputs("IGNORE_FILE_CHECK=1\n", f_out);
    fprintf(f_out, "rotate=%d\n", s_rotate);

    fclose(f_out);
    log_info("Successfully created minimal game.conf (NO_SUBBOARD=1, NO_TOUCHPANEL=1, rotate=%d)", s_rotate);
}

bool kpm_config_get_rotate(void)
{
    return s_rotate != 0;
}
