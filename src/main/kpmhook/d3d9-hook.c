#define LOG_MODULE "kpm-d3d9"

#include <windows.h>
#include <d3d9.h>
#include <stdbool.h>

#include "hook/com-proxy.h"
#include "hook/table.h"
#include "kpmhook/config-kpm.h"
#include "kpmhook/d3d9-hook.h"
#include "kpmhook/io-hook.h"
#include "util/defs.h"
#include "util/log.h"

static bool s_windowed = true;
static bool s_has_dual_station = false;
static bool s_rotate_sub = true;

static IDirect3DSwapChain9 *s_swapchain1 = NULL;
static IDirect3DTexture9 *s_station1_texture = NULL;
static IDirect3DSurface9 *s_station1_surface = NULL;
static HWND s_station0_target_wnd = NULL;
static HWND s_station1_target_wnd = NULL;

static IDirect3D9 *(STDCALL *real_Direct3DCreate9)(UINT sdk_ver);

struct rot_vertex {
    float x, y, z, rhw;
    float u, v;
};
#define D3DFVF_ROT_VERTEX (D3DFVF_XYZRHW | D3DFVF_TEX1)

static void release_rotation_resources(void)
{
    if (s_station1_surface) {
        IDirect3DSurface9_Release(s_station1_surface);
        s_station1_surface = NULL;
    }
    if (s_station1_texture) {
        IDirect3DTexture9_Release(s_station1_texture);
        s_station1_texture = NULL;
    }
    if (s_swapchain1) {
        IDirect3DSwapChain9_Release(s_swapchain1);
        s_swapchain1 = NULL;
    }
}

static void init_rotation_resources(IDirect3DDevice9 *real, UINT backbuf_w, UINT backbuf_h)
{
    release_rotation_resources();

    /* Create Station 1 1024x768 render target texture so game renders directly to it */
    HRESULT hr = IDirect3DDevice9_CreateTexture(
        real, backbuf_w, backbuf_h, 1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8,
        D3DPOOL_DEFAULT,
        &s_station1_texture,
        NULL);
    if (SUCCEEDED(hr) && s_station1_texture) {
        IDirect3DTexture9_GetSurfaceLevel(s_station1_texture, 0, &s_station1_surface);
        log_info("Created Station 1 render target texture: %ux%u (hr=0x%08lx)", backbuf_w, backbuf_h, hr);
    } else {
        log_warning("Failed to create Station 1 render target texture: 0x%08lx", hr);
    }

    /* Create Station 1 presentation swapchain (768x1024 if rotated, else 1024x768) */
    if (s_station1_target_wnd) {
        UINT sc1_w = s_rotate_sub ? backbuf_h : backbuf_w;
        UINT sc1_h = s_rotate_sub ? backbuf_w : backbuf_h;

        D3DPRESENT_PARAMETERS sc_pp;
        memset(&sc_pp, 0, sizeof(sc_pp));
        sc_pp.BackBufferWidth = sc1_w;
        sc_pp.BackBufferHeight = sc1_h;
        sc_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        sc_pp.BackBufferCount = 1;
        sc_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        sc_pp.hDeviceWindow = s_station1_target_wnd;
        sc_pp.Windowed = TRUE;
        sc_pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

        hr = IDirect3DDevice9_CreateAdditionalSwapChain(real, &sc_pp, &s_swapchain1);
        log_info("CreateAdditionalSwapChain for Station 1 (%ux%u, wnd=0x%p) -> hr=0x%08lx, sc=0x%p",
                 sc1_w, sc1_h, s_station1_target_wnd, hr, s_swapchain1);
    }
}

static void present_station1_quad(IDirect3DDevice9 *real)
{
    if (!s_swapchain1 || !s_station1_texture) {
        return;
    }

    IDirect3DSurface9 *sc1_bb = NULL;
    HRESULT hr = IDirect3DSwapChain9_GetBackBuffer(s_swapchain1, 0, D3DBACKBUFFER_TYPE_MONO, &sc1_bb);
    if (FAILED(hr) || !sc1_bb) {
        return;
    }

    /* Save full device state via StateBlock */
    IDirect3DStateBlock9 *sb = NULL;
    IDirect3DDevice9_CreateStateBlock(real, D3DSBT_ALL, &sb);

    IDirect3DSurface9 *orig_rt = NULL;
    IDirect3DSurface9 *orig_ds = NULL;
    IDirect3DDevice9_GetRenderTarget(real, 0, &orig_rt);
    IDirect3DDevice9_GetDepthStencilSurface(real, &orig_ds);

    D3DVIEWPORT9 prev_vp;
    IDirect3DDevice9_GetViewport(real, &prev_vp);

    /* Set swapchain 1 backbuffer as render target */
    IDirect3DDevice9_SetRenderTarget(real, 0, sc1_bb);
    IDirect3DDevice9_SetDepthStencilSurface(real, NULL);

    UINT dst_w = s_rotate_sub ? 768 : 1024;
    UINT dst_h = s_rotate_sub ? 1024 : 768;

    D3DVIEWPORT9 vp;
    vp.X = 0;
    vp.Y = 0;
    vp.Width = dst_w;
    vp.Height = dst_h;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    IDirect3DDevice9_SetViewport(real, &vp);

    struct rot_vertex quad[4];
    float w = (float) dst_w;
    float h = (float) dst_h;

    if (s_rotate_sub) {
        /* 90 degrees CCW rotation mapping:
         * Top-Left:     (0, 0) -> UV=(1, 0)
         * Top-Right:    (w, 0) -> UV=(1, 1)
         * Bottom-Left:  (0, h) -> UV=(0, 0)
         * Bottom-Right: (w, h) -> UV=(0, 1)
         */
        quad[0].x = -0.5f;    quad[0].y = -0.5f;    quad[0].z = 0.0f; quad[0].rhw = 1.0f; quad[0].u = 1.0f; quad[0].v = 0.0f;
        quad[1].x = w - 0.5f; quad[1].y = -0.5f;    quad[1].z = 0.0f; quad[1].rhw = 1.0f; quad[1].u = 1.0f; quad[1].v = 1.0f;
        quad[2].x = -0.5f;    quad[2].y = h - 0.5f; quad[2].z = 0.0f; quad[2].rhw = 1.0f; quad[2].u = 0.0f; quad[2].v = 0.0f;
        quad[3].x = w - 0.5f; quad[3].y = h - 0.5f; quad[3].z = 0.0f; quad[3].rhw = 1.0f; quad[3].u = 0.0f; quad[3].v = 1.0f;
    } else {
        /* Direct landscape blit */
        quad[0].x = -0.5f;    quad[0].y = -0.5f;    quad[0].z = 0.0f; quad[0].rhw = 1.0f; quad[0].u = 0.0f; quad[0].v = 0.0f;
        quad[1].x = w - 0.5f; quad[1].y = -0.5f;    quad[1].z = 0.0f; quad[1].rhw = 1.0f; quad[1].u = 1.0f; quad[0].v = 0.0f;
        quad[2].x = -0.5f;    quad[2].y = h - 0.5f; quad[2].z = 0.0f; quad[2].rhw = 1.0f; quad[2].u = 0.0f; quad[2].v = 1.0f;
        quad[3].x = w - 0.5f; quad[3].y = h - 0.5f; quad[3].z = 0.0f; quad[3].rhw = 1.0f; quad[3].u = 1.0f; quad[3].v = 1.0f;
    }

    IDirect3DDevice9_SetVertexShader(real, NULL);
    IDirect3DDevice9_SetPixelShader(real, NULL);
    IDirect3DDevice9_SetFVF(real, D3DFVF_ROT_VERTEX);
    IDirect3DDevice9_SetRenderState(real, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(real, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(real, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(real, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetTexture(real, 0, (IDirect3DBaseTexture9 *) s_station1_texture);
    IDirect3DDevice9_SetTextureStageState(real, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(real, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(real, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(real, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(real, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(real, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(real, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(real, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    IDirect3DDevice9_BeginScene(real);
    IDirect3DDevice9_DrawPrimitiveUP(real, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(struct rot_vertex));
    IDirect3DDevice9_EndScene(real);

    /* Explicitly unbind texture from stage 0 before state restoration */
    IDirect3DDevice9_SetTexture(real, 0, NULL);

    /* Restore all device states */
    if (sb) {
        IDirect3DStateBlock9_Apply(sb);
        IDirect3DStateBlock9_Release(sb);
    }
    IDirect3DDevice9_SetViewport(real, &prev_vp);

    if (orig_rt) {
        IDirect3DDevice9_SetRenderTarget(real, 0, orig_rt);
        IDirect3DSurface9_Release(orig_rt);
    }
    if (orig_ds) {
        IDirect3DDevice9_SetDepthStencilSurface(real, orig_ds);
        IDirect3DSurface9_Release(orig_ds);
    }

    IDirect3DSurface9_Release(sc1_bb);

    IDirect3DSwapChain9_Present(s_swapchain1, NULL, NULL, NULL, NULL, 0);
}

static HRESULT STDMETHODCALLTYPE my_GetDeviceCaps(
    IDirect3D9 *self,
    UINT adapter,
    D3DDEVTYPE device_type,
    D3DCAPS9 *caps)
{
    IDirect3D9 *real = (IDirect3D9 *) com_proxy_downcast(self)->real;
    HRESULT hr = IDirect3D9_GetDeviceCaps(real, adapter, device_type, caps);
    if (SUCCEEDED(hr) && caps) {
        log_info("GetDeviceCaps: Adapter=%u Type=%u -> hr=0x%08lx (orig NumberOfAdaptersInGroup=%u -> reporting 2)",
                 adapter, device_type, hr, caps->NumberOfAdaptersInGroup);
        caps->NumberOfAdaptersInGroup = 2;
    } else {
        log_info("GetDeviceCaps: Adapter=%u Type=%u -> hr=0x%08lx", adapter, device_type, hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE my_CheckDeviceType(
    IDirect3D9 *self,
    UINT adapter,
    D3DDEVTYPE check_type,
    D3DFORMAT display_format,
    D3DFORMAT backbuffer_format,
    BOOL windowed)
{
    IDirect3D9 *real = (IDirect3D9 *) com_proxy_downcast(self)->real;
    HRESULT hr;

    if (s_windowed) {
        windowed = TRUE;
    }

    hr = IDirect3D9_CheckDeviceType(real, adapter, check_type, display_format, backbuffer_format, windowed);
    log_info("CheckDeviceType: Adapter=%u Type=%u DispFmt=%u BBFmt=%u Win=%d -> hr=0x%08lx",
             adapter, check_type, display_format, backbuffer_format, windowed, hr);

    return hr;
}

static HRESULT STDMETHODCALLTYPE my_Reset(
    IDirect3DDevice9 *self,
    D3DPRESENT_PARAMETERS *pp)
{
    IDirect3DDevice9 *real = (IDirect3DDevice9 *) com_proxy_downcast(self)->real;
    HRESULT hr;

    log_info("IDirect3DDevice9::Reset called (has_dual_station=%d)", s_has_dual_station);
    release_rotation_resources();

    D3DPRESENT_PARAMETERS *pp0 = pp;
    UINT backbuf_w = 1024;
    UINT backbuf_h = 768;

    if (pp0) {
        if (pp0->BackBufferWidth) backbuf_w = pp0->BackBufferWidth;
        if (pp0->BackBufferHeight) backbuf_h = pp0->BackBufferHeight;

        if (s_windowed) {
            pp0->Windowed = TRUE;
            pp0->FullScreen_RefreshRateInHz = 0;
        }
        if (pp0->SwapEffect == D3DSWAPEFFECT_DISCARD && (pp0->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER)) {
            pp0->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        }
        if (s_station0_target_wnd) {
            pp0->hDeviceWindow = s_station0_target_wnd;
        }
    }

    hr = IDirect3DDevice9_Reset(real, pp0);
    log_info("IDirect3DDevice9::Reset returned hr=0x%08lx", hr);

    if (SUCCEEDED(hr)) {
        init_rotation_resources(real, backbuf_w, backbuf_h);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE my_Present(
    IDirect3DDevice9 *self,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion)
{
    IDirect3DDevice9 *real = (IDirect3DDevice9 *) com_proxy_downcast(self)->real;

    /* Update arcade inputs and PCSub simulation */
    kpm_io_hook_update();

    /* Present Station 1 (Sub Screen) */
    if (s_has_dual_station) {
        present_station1_quad(real);
    }

    /* Present Station 0 (Main Screen) natively */
    return IDirect3DDevice9_Present(real, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

static HRESULT STDMETHODCALLTYPE my_GetBackBuffer(
    IDirect3DDevice9 *self,
    UINT iSwapChain,
    UINT iBackBuffer,
    D3DBACKBUFFER_TYPE Type,
    IDirect3DSurface9 **ppBackBuffer)
{
    IDirect3DDevice9 *real = (IDirect3DDevice9 *) com_proxy_downcast(self)->real;
    if (iSwapChain == 0) {
        return IDirect3DDevice9_GetBackBuffer(real, 0, iBackBuffer, Type, ppBackBuffer);
    } else if (iSwapChain == 1) {
        if (s_station1_surface) {
            *ppBackBuffer = s_station1_surface;
            IDirect3DSurface9_AddRef(s_station1_surface);
            return D3D_OK;
        } else if (s_swapchain1) {
            return IDirect3DSwapChain9_GetBackBuffer(s_swapchain1, iBackBuffer, Type, ppBackBuffer);
        }
    }
    return D3DERR_INVALIDCALL;
}

static UINT STDMETHODCALLTYPE my_GetNumberOfSwapChains(IDirect3DDevice9 *self)
{
    return s_has_dual_station ? 2 : 1;
}

static HRESULT STDMETHODCALLTYPE my_GetSwapChain(
    IDirect3DDevice9 *self,
    UINT iSwapChain,
    IDirect3DSwapChain9 **pSwapChain)
{
    IDirect3DDevice9 *real = (IDirect3DDevice9 *) com_proxy_downcast(self)->real;
    if (iSwapChain == 0) {
        return IDirect3DDevice9_GetSwapChain(real, 0, pSwapChain);
    } else if (iSwapChain == 1 && s_swapchain1) {
        *pSwapChain = s_swapchain1;
        IDirect3DSwapChain9_AddRef(s_swapchain1);
        return D3D_OK;
    }
    return D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE my_CreateDevice(
    IDirect3D9 *self,
    UINT adapter,
    D3DDEVTYPE type,
    HWND hwnd,
    DWORD flags,
    D3DPRESENT_PARAMETERS *pp,
    IDirect3DDevice9 **pdev)
{
    IDirect3D9 *real = (IDirect3D9 *) com_proxy_downcast(self)->real;
    HRESULT hr;

    s_rotate_sub = kpm_config_get_rotate();

    bool is_adapter_group = (flags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0;
    D3DPRESENT_PARAMETERS *pp0 = pp;
    D3DPRESENT_PARAMETERS *pp1 = is_adapter_group ? &pp[1] : NULL;
    s_has_dual_station = is_adapter_group;

    log_info("IDirect3D9::CreateDevice called: Adapter=%u, DevType=%u, hwnd=0x%p, Flags=0x%08lx (adapter_group=%d, rotate_sub=%d)",
             adapter, type, hwnd, flags, is_adapter_group, s_rotate_sub);

    /* DXVK and consumer GPUs do not support D3DCREATE_ADAPTERGROUP_DEVICE; strip it */
    flags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;

    UINT backbuf_w = 1024;
    UINT backbuf_h = 768;

    if (pp0) {
        s_station0_target_wnd = pp0->hDeviceWindow ? pp0->hDeviceWindow : hwnd;
        if (pp0->BackBufferWidth) backbuf_w = pp0->BackBufferWidth;
        if (pp0->BackBufferHeight) backbuf_h = pp0->BackBufferHeight;

        log_info("  PP[0]: %ux%u fmt=%u count=%u ms=%u q=%u swap=%u wnd=0x%p win=%d depth=%d dfmt=%u flags=0x%08lx hz=%u int=0x%08lx",
                 pp0->BackBufferWidth, pp0->BackBufferHeight, pp0->BackBufferFormat, pp0->BackBufferCount,
                 pp0->MultiSampleType, pp0->MultiSampleQuality, pp0->SwapEffect, pp0->hDeviceWindow,
                 pp0->Windowed, pp0->EnableAutoDepthStencil, pp0->AutoDepthStencilFormat,
                 pp0->Flags, pp0->FullScreen_RefreshRateInHz, pp0->PresentationInterval);

        if (s_windowed) {
            pp0->Windowed = TRUE;
            pp0->FullScreen_RefreshRateInHz = 0;
        }

        if (pp0->SwapEffect == D3DSWAPEFFECT_DISCARD && (pp0->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER)) {
            log_info("  Fixing incompatible D3DPRESENTFLAG_LOCKABLE_BACKBUFFER with D3DSWAPEFFECT_DISCARD on PP[0]");
            pp0->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        }

        pp0->hDeviceWindow = s_station0_target_wnd;
    }

    if (pp1) {
        s_station1_target_wnd = pp1->hDeviceWindow;

        log_info("  PP[1]: %ux%u fmt=%u count=%u ms=%u q=%u swap=%u wnd=0x%p win=%d depth=%d dfmt=%u flags=0x%08lx hz=%u int=0x%08lx",
                 pp1->BackBufferWidth, pp1->BackBufferHeight, pp1->BackBufferFormat, pp1->BackBufferCount,
                 pp1->MultiSampleType, pp1->MultiSampleQuality, pp1->SwapEffect, pp1->hDeviceWindow,
                 pp1->Windowed, pp1->EnableAutoDepthStencil, pp1->AutoDepthStencilFormat,
                 pp1->Flags, pp1->FullScreen_RefreshRateInHz, pp1->PresentationInterval);

        if (s_windowed) {
            pp1->Windowed = TRUE;
            pp1->FullScreen_RefreshRateInHz = 0;
        }

        if (pp1->SwapEffect == D3DSWAPEFFECT_DISCARD && (pp1->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER)) {
            log_info("  Fixing incompatible D3DPRESENTFLAG_LOCKABLE_BACKBUFFER with D3DSWAPEFFECT_DISCARD on PP[1]");
            pp1->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        }
    }

    HWND dev_wnd = s_station0_target_wnd ? s_station0_target_wnd : hwnd;
    hr = IDirect3D9_CreateDevice(real, adapter, type, dev_wnd, flags, pp0, pdev);
    log_info("IDirect3D9::CreateDevice returned hr=0x%08lx", hr);

    if (FAILED(hr) && (flags & D3DCREATE_HARDWARE_VERTEXPROCESSING)) {
        DWORD fallback_flags = (flags & ~D3DCREATE_HARDWARE_VERTEXPROCESSING) | D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        log_warning("CreateDevice failed, retrying with SOFTWARE_VERTEXPROCESSING (flags=0x%08lx)...", fallback_flags);
        hr = IDirect3D9_CreateDevice(real, adapter, type, dev_wnd, fallback_flags, pp0, pdev);
        log_info("Retry CreateDevice returned hr=0x%08lx", hr);
    }

    if (FAILED(hr) && type == D3DDEVTYPE_REF) {
        log_warning("CreateDevice requested REF, retrying with D3DDEVTYPE_HAL on adapter %u...", adapter);
        hr = IDirect3D9_CreateDevice(real, adapter, D3DDEVTYPE_HAL, dev_wnd, flags, pp0, pdev);
        log_info("HAL retry CreateDevice returned hr=0x%08lx", hr);
    }

    if (SUCCEEDED(hr) && pdev && *pdev) {
        init_rotation_resources(*pdev, backbuf_w, backbuf_h);

        struct com_proxy *dev_proxy = NULL;
        HRESULT wrap_hr = com_proxy_wrap(&dev_proxy, *pdev, sizeof(IDirect3DDevice9Vtbl));
        if (SUCCEEDED(wrap_hr) && dev_proxy) {
            IDirect3DDevice9Vtbl *dev_vtbl = (IDirect3DDevice9Vtbl *) dev_proxy->vptr;
            dev_vtbl->Reset = my_Reset;
            dev_vtbl->Present = my_Present;
            dev_vtbl->GetBackBuffer = my_GetBackBuffer;
            dev_vtbl->GetNumberOfSwapChains = my_GetNumberOfSwapChains;
            dev_vtbl->GetSwapChain = my_GetSwapChain;
            *pdev = (IDirect3DDevice9 *) dev_proxy;
            log_info("IDirect3DDevice9 wrapped with com_proxy successfully (proxy=0x%p)", dev_proxy);
        } else {
            log_warning("Failed to wrap IDirect3DDevice9: 0x%08lx", wrap_hr);
        }
    }

    return hr;
}

static IDirect3D9 *STDCALL my_Direct3DCreate9(UINT sdk_ver)
{
    IDirect3D9 *api = NULL;
    IDirect3D9Vtbl *api_vtbl;
    struct com_proxy *proxy;
    HRESULT hr;

    log_info("Direct3DCreate9(sdk_ver=%u) called (routing to DXVK/D3D9)", sdk_ver);

    api = real_Direct3DCreate9(sdk_ver);
    if (!api) {
        log_fatal("Failed to create Direct3D9 object from real_Direct3DCreate9!");
        return NULL;
    }

    hr = com_proxy_wrap(&proxy, api, sizeof(*api->lpVtbl));
    if (FAILED(hr)) {
        log_warning("com_proxy_wrap failed: 0x%08lx", hr);
        return api;
    }

    api_vtbl = (IDirect3D9Vtbl *) proxy->vptr;
    api_vtbl->GetDeviceCaps = my_GetDeviceCaps;
    api_vtbl->CheckDeviceType = my_CheckDeviceType;
    api_vtbl->CreateDevice = my_CreateDevice;

    log_info("IDirect3D9 proxy hooked successfully (DXVK backend)");
    return (IDirect3D9 *) proxy;
}

static const struct hook_symbol kpm_d3d9_syms[] = {
    {
        .name = "Direct3DCreate9",
        .patch = my_Direct3DCreate9,
        .link = (void **) &real_Direct3DCreate9,
    },
};

void kpm_d3d9_hook_init(bool windowed)
{
    s_windowed = windowed;

    hook_table_apply(
        NULL,
        "d3d9.dll",
        kpm_d3d9_syms,
        lengthof(kpm_d3d9_syms));

    log_info("Direct3D9 API hook installed (windowed=%d, DXVK-compatible)", windowed);
}
