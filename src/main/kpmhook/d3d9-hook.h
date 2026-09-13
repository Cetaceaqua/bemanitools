#ifndef KPMHOOK_D3D9_HOOK_H
#define KPMHOOK_D3D9_HOOK_H

#include <stdbool.h>
#include <d3d9.h>

void kpm_d3d9_hook_init(bool windowed);

IDirect3D9 *kpm_d3d9_get_real_d3d9(void);
IDirect3DDevice9 *kpm_d3d9_get_real_device(void);
IDirect3DDevice9 *kpm_d3d9_get_device_proxy(void);

#endif /* KPMHOOK_D3D9_HOOK_H */

