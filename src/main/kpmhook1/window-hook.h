#ifndef KPMHOOK1_WINDOW_HOOK_H
#define KPMHOOK1_WINDOW_HOOK_H

#include <windows.h>
#include <stdbool.h>

/**
 * Initialize window management hooks:
 * - Subclasses Station 0 (Main Screen) and Station 1 (Sub Screen)
 * - Enables window dragging via title bar
 * - Enables minimize, maximize, and close buttons
 * - Places both station windows side-by-side with titles and frames
 * - Prevents topmost and forced coordinate resets
 */
void kpm_window_hook_init(void);

#endif
