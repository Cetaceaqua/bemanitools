#ifndef KPMHOOK_PATH_HOOK_H
#define KPMHOOK_PATH_HOOK_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize kernel32 IAT hooks for redirecting hardcoded D:\KPM and E:\ paths.
 */
void kpm_path_hook_init(void);

/**
 * Rewrite wide path from D:\KPM or E:\ to the local game directory.
 */
bool kpm_path_rewrite_w(const wchar_t *in_path, wchar_t *out_path, size_t out_len, bool is_write);

#endif

