#ifndef KPMHOOK1_CONFIG_KPM_H
#define KPMHOOK1_CONFIG_KPM_H

#include <stdbool.h>

struct kpmhook1_config {
    bool windowed;
    int station;
};

/**
 * Check and bootstrap game.conf with optimal Windows 11 compatibility settings.
 */
void kpm_config_bootstrap(void);

/**
 * Check if display should be rotated 90 degrees CCW (portrait mode).
 * Defaults to true unless rotate=0 is set in game.conf.
 */
bool kpm_config_get_rotate(void);

#endif
