#ifndef KPMHOOK1_PATH_HOOK_H
#define KPMHOOK1_PATH_HOOK_H

/**
 * Initialize kernel32 IAT hooks for redirecting hardcoded D:\KPM and E:\ paths.
 */
void kpm_path_hook_init(void);

#endif
