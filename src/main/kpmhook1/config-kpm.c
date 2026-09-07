#define LOG_MODULE "kpm-conf"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kpmhook1/config-kpm.h"
#include "util/log.h"

void kpm_config_bootstrap(void)
{
    char root[MAX_PATH];
    char conf_path[MAX_PATH];
    char *last_sep;
    FILE *f_out;

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
    fputs("NO_CARDREADER=1\n", f_out);
    fputs("IGNORE_IRCOM=1\n", f_out);
    fputs("DISABLE_ALL_ERROR=1\n", f_out);
    fputs("IGNORE_FILE_CHECK=1\n", f_out);
    fputs("rotate=1\n", f_out);

    fclose(f_out);
    log_info("Successfully created minimal game.conf (NO_SUBBOARD=1, NO_TOUCHPANEL=1, etc.)");
}

bool kpm_config_get_rotate(void)
{
    static int cached_rotate = -1;
    if (cached_rotate != -1) {
        return cached_rotate != 0;
    }

    /* Default to true (portrait 90 deg CCW) for arcade display */
    cached_rotate = 1;

    char root[MAX_PATH];
    char conf_path[MAX_PATH];
    char *last_sep;

    GetModuleFileNameA(NULL, root, sizeof(root));
    last_sep = strrchr(root, '\\');
    if (!last_sep) {
        last_sep = strrchr(root, '/');
    }
    if (last_sep) {
        *last_sep = '\0';
    }

    _snprintf(conf_path, sizeof(conf_path), "%s\\game.conf", root);

    FILE *f = fopen(conf_path, "rt");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (_strnicmp(line, "rotate=", 7) == 0) {
                int val = atoi(line + 7);
                cached_rotate = (val != 0) ? 1 : 0;
                break;
            }
        }
        fclose(f);
    }

    log_info("Display rotation (90 deg CCW portrait): %s", cached_rotate ? "ENABLED" : "DISABLED");
    return cached_rotate != 0;
}
