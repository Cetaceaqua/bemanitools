#ifndef KPMHOOK1_GFX_PATCH_H
#define KPMHOOK1_GFX_PATCH_H

#include <stdbool.h>

/**
 * Verify target executable fingerprint before applying any memory patches.
 * Returns true if target matches KT_SKELETON_ST_DUAL.EXE (2012-03-07 build).
 */
bool kpm_gfx_patch_verify(void);

/**
 * Apply D3D9 windowed mode patches to game engine memory.
 */
void kpm_gfx_patch_apply(bool windowed);

#endif
