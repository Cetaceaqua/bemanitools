#define LOG_MODULE "kpm-camera"

#include <windows.h>
#include <objbase.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kpmhook/camera-hook.h"
#include "util/log.h"

/* DirectShow GUIDs */
static const GUID CLSID_SystemDeviceEnum_Val =
    { 0x62BE5D10, 0x60EB, 0x11D0, { 0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86 } };

static const GUID CLSID_VideoInputDeviceCategory_Val =
    { 0x860BB310, 0x5D01, 0x11D0, { 0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86 } };

static const GUID IID_ICreateDevEnum_Val =
    { 0x29840822, 0x5B84, 0x11D0, { 0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86 } };

static const GUID IID_IPropertyBag_Val =
    { 0x55272A00, 0x42CB, 0x11CE, { 0x81, 0x35, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51 } };

/* Direct COM vtable interface for ICreateDevEnum to avoid Windows SDK header differences */
typedef struct ICreateDevEnumVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *this_ptr, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *this_ptr);
    ULONG   (STDMETHODCALLTYPE *Release)(void *this_ptr);
    HRESULT (STDMETHODCALLTYPE *CreateClassEnumerator)(
        void *this_ptr,
        const GUID *pType,
        IEnumMoniker **ppEnumMoniker,
        DWORD dwFlags);
} ICreateDevEnumVtbl;

typedef struct ICreateDevEnum_Custom {
    ICreateDevEnumVtbl *lpVtbl;
} ICreateDevEnum_Custom;

/* Direct COM vtable interface for ISampleGrabber */
typedef HRESULT (STDMETHODCALLTYPE *GetCurrentBufferFn)(void *this_ptr, long *pBufferSize, long *pBuffer);

/* -------------------------------------------------------------------------
 * Memory Patching Helpers
 * ------------------------------------------------------------------------- */
static void patch_memory(uintptr_t addr, const void *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
}

static void patch_jmp(uintptr_t hook_addr, const void *target_func, size_t total_len)
{
    uint8_t buf[32];
    if (total_len < 5 || total_len > sizeof(buf)) return;

    buf[0] = 0xE9;
    *((int32_t *) &buf[1]) = (int32_t) ((uintptr_t) target_func - (hook_addr + 5));
    if (total_len > 5) {
        memset(&buf[5], 0x90, total_len - 5);
    }
    patch_memory(hook_addr, buf, total_len);
}

/* -------------------------------------------------------------------------
 * DirectShow Video Input Device Enumeration
 * ------------------------------------------------------------------------- */
static void kpm_camera_enum_devices(void)
{
    log_info("Enumerating DirectShow video capture devices...");

    HRESULT hr_co = CoInitialize(NULL);

    ICreateDevEnum_Custom *dev_enum = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_SystemDeviceEnum_Val, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_ICreateDevEnum_Val, (void **) &dev_enum);
    if (FAILED(hr) || !dev_enum) {
        log_warning("Failed to create CLSID_SystemDeviceEnum (hr=0x%08lX)", hr);
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return;
    }

    IEnumMoniker *enum_mon = NULL;
    hr = dev_enum->lpVtbl->CreateClassEnumerator(dev_enum, &CLSID_VideoInputDeviceCategory_Val, &enum_mon, 0);
    if (hr != S_OK || !enum_mon) {
        log_info("No DirectShow video capture devices found (CreateClassEnumerator returned 0x%08lX)", hr);
        dev_enum->lpVtbl->Release(dev_enum);
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return;
    }

    IMoniker *moniker = NULL;
    ULONG fetched = 0;
    int count = 0;

    while (enum_mon->lpVtbl->Next(enum_mon, 1, &moniker, &fetched) == S_OK && moniker) {
        IPropertyBag *prop_bag = NULL;
        hr = moniker->lpVtbl->BindToStorage(moniker, NULL, NULL, &IID_IPropertyBag_Val, (void **) &prop_bag);
        if (SUCCEEDED(hr) && prop_bag) {
            VARIANT var_name;
            VARIANT var_path;
            VariantInit(&var_name);
            VariantInit(&var_path);

            char friendly_name_a[256] = "Unknown";
            char device_path_a[512] = "";

            if (SUCCEEDED(prop_bag->lpVtbl->Read(prop_bag, L"FriendlyName", &var_name, NULL)) &&
                var_name.vt == VT_BSTR && var_name.bstrVal) {
                WideCharToMultiByte(CP_UTF8, 0, var_name.bstrVal, -1, friendly_name_a, sizeof(friendly_name_a), NULL, NULL);
            }
            if (SUCCEEDED(prop_bag->lpVtbl->Read(prop_bag, L"DevicePath", &var_path, NULL)) &&
                var_path.vt == VT_BSTR && var_path.bstrVal) {
                WideCharToMultiByte(CP_UTF8, 0, var_path.bstrVal, -1, device_path_a, sizeof(device_path_a), NULL, NULL);
            }

            log_info("  [Camera Device %d] '%s' (Path: %s)", count, friendly_name_a, device_path_a);

            VariantClear(&var_name);
            VariantClear(&var_path);
            prop_bag->lpVtbl->Release(prop_bag);
        } else {
            log_info("  [Camera Device %d] (Unable to query IPropertyBag)", count);
        }

        moniker->lpVtbl->Release(moniker);
        moniker = NULL;
        count++;
    }

    log_info("DirectShow device enumeration complete: %d camera(s) detected", count);

    enum_mon->lpVtbl->Release(enum_mon);
    dev_enum->lpVtbl->Release(dev_enum);
    if (SUCCEEDED(hr_co)) CoUninitialize();
}

/* -------------------------------------------------------------------------
 * Hook 1: CCameraDevice::SetWindow (0x0041C730)
 * Prevents null pointer dereference in CARCertify when camera is absent
 * ------------------------------------------------------------------------- */
static const uintptr_t SUB_41C730_ADDR = 0x0041C730;
static const uintptr_t SUB_41C730_CONT = 0x0041C737;

static void log_camera_null_device(void)
{
    log_info("CCameraDevice::SetWindow(0x0041C730): this (eax) is NULL (camera disabled or absent). Handled safely.");
}

static __declspec(naked) void sub_41C730_hook(void)
{
    __asm {
        test eax, eax
        jnz normal_path

        pushad
        call log_camera_null_device
        popad

        xor eax, eax
        ret 8

    normal_path:
        mov ecx, [eax + 0x44]
        add ecx, [esp + 4]
        jmp dword ptr [SUB_41C730_CONT]
    }
}

/* -------------------------------------------------------------------------
 * Hook 2: CCameraDevice::StartCapture First-Frame Wait Loop (0x0041CE30)
 * Replaces 100% CPU infinite spin loop with Sleep(10) and a 5-second timeout
 * ------------------------------------------------------------------------- */
static const uintptr_t SUB_41CE30_ADDR = 0x0041CE30;
static const uintptr_t SUB_41CE45_ADDR = 0x0041CE45;

static void my_camera_wait_first_frame(void *sample_grabber, long *p_buf_size)
{
    if (!sample_grabber || !p_buf_size) return;

    void **vtable = *(void ***) sample_grabber;
    GetCurrentBufferFn get_buf = (GetCurrentBufferFn) vtable[7];

    *p_buf_size = 0;
    log_info("Waiting for first camera frame from DirectShow ISampleGrabber...");

    for (int attempt = 0; attempt < 500; attempt++) {
        HRESULT hr = get_buf(sample_grabber, p_buf_size, NULL);
        if (SUCCEEDED(hr) && *p_buf_size > 0) {
            log_info("First camera frame received! Buffer size: %ld bytes (waited %d ms)",
                     *p_buf_size, attempt * 10);
            return;
        }
        Sleep(10);
    }

    log_warning("Timeout waiting for first camera frame (5 seconds). Buffer size: %ld", *p_buf_size);
    /* Fallback default size (640x480x2 YUY2) if camera is slow or failed */
    if (*p_buf_size <= 0) {
        *p_buf_size = 640 * 480 * 2;
    }
}

static __declspec(naked) void sub_41ce30_hook(void)
{
    __asm {
        mov eax, [ebx]          // ISampleGrabber pointer
        lea edi, [esi + 0x1C]   // &this->m_buffer_size
        pushad
        push edi
        push eax
        call my_camera_wait_first_frame
        add esp, 8
        popad
        jmp dword ptr [SUB_41CE45_ADDR]
    }
}

/* -------------------------------------------------------------------------
 * Hook 3: CCameraDevice::ThreadProc (0x0041D3E9)
 * Prevents crash if capture thread runs when ISampleGrabber is NULL
 * ------------------------------------------------------------------------- */
static const uintptr_t SUB_41D3E9_ADDR = 0x0041D3E9;
static const uintptr_t SUB_41D3E9_CONT = 0x0041D3F1;
static const uintptr_t SUB_41D3E9_EXIT = 0x0041D428;

static void log_camera_thread_null_grabber(void)
{
    log_warning("CCameraDevice::ThreadProc (0x0041D3E9): ISampleGrabber is NULL; exiting thread safely.");
}

static __declspec(naked) void sub_41d3e9_hook(void)
{
    __asm {
        mov eax, [esi + 0x4C]   // ISampleGrabber pointer
        test eax, eax
        jz null_grabber

        mov ecx, [eax]
        mov edx, [ecx + 0x18]
        jmp dword ptr [SUB_41D3E9_CONT]

    null_grabber:
        pushad
        call log_camera_thread_null_grabber
        popad
        jmp dword ptr [SUB_41D3E9_EXIT]
    }
}

/* -------------------------------------------------------------------------
 * Public Initialization
 * ------------------------------------------------------------------------- */
void kpm_camera_hook_init(void)
{
    /* 1. Enumerate available DirectShow cameras and log them */
    kpm_camera_enum_devices();

    /* 2. Install CCameraDevice::SetWindow null safety hook (7 bytes) */
    patch_jmp(SUB_41C730_ADDR, sub_41C730_hook, 7);
    log_info("Installed CCameraDevice::SetWindow null safety hook at 0x%08X", SUB_41C730_ADDR);

    /* 3. Install first-frame wait loop polling hook (21 bytes) */
    patch_jmp(SUB_41CE30_ADDR, sub_41ce30_hook, 21);
    log_info("Installed CCameraDevice::StartCapture wait loop hook at 0x%08X", SUB_41CE30_ADDR);

    /* 4. Install ThreadProc ISampleGrabber null safety hook (8 bytes) */
    patch_jmp(SUB_41D3E9_ADDR, sub_41d3e9_hook, 8);
    log_info("Installed CCameraDevice::ThreadProc grabber null hook at 0x%08X", SUB_41D3E9_ADDR);

    log_info("Camera subsystem initialized successfully.");
}
