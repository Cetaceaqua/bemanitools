#ifndef KPMHOOK_IO_HOOK_H
#define KPMHOOK_IO_HOOK_H

#include <stdbool.h>

/**
 * Initialize PCSub arcade I/O virtualization and patch input polling.
 */
void kpm_io_hook_init(void);

/**
 * Update I/O state (poll kpmio backend, update PCSub button and medal buffers).
 * Called once per frame (e.g. from Direct3D Present).
 */
void kpm_io_hook_update(void);

#endif /* KPMHOOK_IO_HOOK_H */
