#ifndef KPMHOOK_MOVIE_HOOK_H
#define KPMHOOK_MOVIE_HOOK_H

#include "kpmhook/config-io.h"

/**
 * Initialize DirectShow VMR9 movie playback hooks, COM apartment, and Step 17 watchdog.
 */
void kpm_movie_hook_init(const struct kpmhook_config_io *cfg);

#endif /* KPMHOOK_MOVIE_HOOK_H */
