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
    char def_conf_path[MAX_PATH];
    char *last_sep;
    FILE *f_in;
    FILE *f_out;
    char line[1024];

    GetModuleFileNameA(NULL, root, sizeof(root));
    last_sep = strrchr(root, '\\');
    if (!last_sep) {
        last_sep = strrchr(root, '/');
    }
    if (last_sep) {
        *last_sep = '\0';
    }

    _snprintf(conf_path, sizeof(conf_path), "%s\\game.conf", root);
    _snprintf(def_conf_path, sizeof(def_conf_path), "%s\\default_game.conf", root);

    /* If game.conf already exists, do nothing */
    if (GetFileAttributesA(conf_path) != INVALID_FILE_ATTRIBUTES) {
        log_info("Found existing game.conf at %s", conf_path);
        return;
    }

    log_info("game.conf not found. Bootstrapping compatibility config from default_game.conf...");

    f_in = fopen(def_conf_path, "rt");
    f_out = fopen(conf_path, "wt");

    if (!f_out) {
        log_warning("Failed to create %s for writing", conf_path);
        if (f_in) fclose(f_in);
        return;
    }

    if (f_in) {
        /* Filter/override known crash-inducing options */
        while (fgets(line, sizeof(line), f_in)) {
            if (_strnicmp(line, "station=", 8) == 0) {
                fputs("station=1\n", f_out);
            } else if (_strnicmp(line, "NO_TOUCHPANEL=", 14) == 0) {
                fputs("NO_TOUCHPANEL=1\n", f_out);
            } else if (_strnicmp(line, "NO_SUBBOARD=", 12) == 0) {
                fputs("NO_SUBBOARD=1\n", f_out);
            } else if (_strnicmp(line, "IGNORE_IRCOM=", 13) == 0) {
                fputs("IGNORE_IRCOM=1\n", f_out);
            } else if (_strnicmp(line, "NO_CARDREADER=", 14) == 0) {
                fputs("NO_CARDREADER=1\n", f_out);
            } else if (_strnicmp(line, "DISABLE_ALL_ERROR=", 18) == 0) {
                fputs("DISABLE_ALL_ERROR=1\n", f_out);
            } else if (_strnicmp(line, "IGNORE_FILE_CHECK=", 18) == 0) {
                fputs("IGNORE_FILE_CHECK=1\n", f_out);
            } else {
                fputs(line, f_out);
            }
        }
        fclose(f_in);
    } else {
        /* Generate minimal fallback game.conf */
        fputs("width=1024\n", f_out);
        fputs("height=768\n", f_out);
        fputs("station=1\n", f_out);
        fputs("sound_ram=256\n", f_out);
        fputs("sound_s3b=\"d:/KPM/data/sound/sound.s3b\"\n", f_out);
        fputs("temp_dir=\"E:\\\\temp\\\\\"\n", f_out);
        fputs("NO_TOUCHPANEL=1\n", f_out);
        fputs("NO_SUBBOARD=1\n", f_out);
        fputs("IGNORE_IRCOM=1\n", f_out);
        fputs("NO_CARDREADER=1\n", f_out);
        fputs("DISABLE_ALL_ERROR=1\n", f_out);
        fputs("IGNORE_FILE_CHECK=1\n", f_out);
    }

    fclose(f_out);
    log_info("Successfully created game.conf with Windows 11 compatibility options (station=1, NO_SUBBOARD=1, NO_TOUCHPANEL=1, etc.)");
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
