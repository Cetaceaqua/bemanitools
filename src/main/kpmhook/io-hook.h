#ifndef KPMHOOK_IO_HOOK_H
#define KPMHOOK_IO_HOOK_H

#include <stdbool.h>
#include "kpmhook/config-io.h"

/**
 * Initialize PCSub arcade I/O virtualization, patch input polling, and setup
 * cabinet lamp and raw LED streaming subsystems.
 *
 * @param cfg Configuration structure for I/O, debug keys, and lighting.
 */
void kpm_io_hook_init(const struct kpmhook_config_io *cfg);

/**
 * Update I/O state (poll kpmio backend, update PCSub button and medal buffers,
 * update cabinet illumination state machine and dispatch lamp states).
 * Called once per frame (from Direct3D Present).
 */
void kpm_io_hook_update(void);

/**
 * Flush and persist SRAM NVRAM to disk on process shutdown or request.
 */
void kpm_io_hook_fini(void);

/**
 * Synchronously flush SRAM NVRAM to disk if changed.
 */
void kpm_io_hook_flush_nvram(void);

#endif /* KPMHOOK_IO_HOOK_H */

