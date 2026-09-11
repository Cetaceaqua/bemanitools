#define LOG_MODULE "kpm-gfx"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hook/pe.h"
#include "kpmhook/gfx-patch.h"
#include "util/log.h"

static void kpm_gfx_install_texture_crash_hook(void);
static void kpm_station_install_safety_hook(void);

/* Address of: mov byte ptr ds:[19BD328h], 1 in KtDirect3D9_Initialize */
#define ADDR_FULLSCREEN_ALLOWED_MOV  ((void *) 0x0058254E)
static const uint8_t EXPECTED_FULLSCREEN_ALLOWED_BYTES[] = {
    0xC6, 0x05, 0x28, 0xD3, 0x9B, 0x01, 0x01
};

/* Address of: mov dword ptr [eax+20h], 0 in KtDirect3DGpu_BuildPerStationPresentParams */
#define ADDR_PER_STATION_WINDOWED_MOV ((void *) 0x005829DC)
static const uint8_t EXPECTED_PER_STATION_WINDOWED_BYTES[] = {
    0xC7, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00
};

/* Address of: mov dword ptr [esp+10h], 15h in sub_583A60 (Backbuffer format selection) */
#define ADDR_BACKBUFFER_FORMAT_MOV ((void *) 0x00583ACD)
static const uint8_t EXPECTED_BACKBUFFER_FORMAT_BYTES[] = {
    0xC7, 0x44, 0x24, 0x10, 0x15, 0x00, 0x00, 0x00
};

bool kpm_gfx_patch_verify(void)
{
    char exe_path[MAX_PATH];
    DWORD len;

    len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (len == 0) {
        log_warning("Failed to get main module filename");
        return false;
    }

    if (strstr(exe_path, "KT_SKELETON_ST_DUAL") == NULL) {
        log_warning("Executable name (%s) does not match KT_SKELETON_ST_DUAL", exe_path);
        return false;
    }

    /* Verify code fingerprints at known addresses */
    if (memcmp(ADDR_FULLSCREEN_ALLOWED_MOV, EXPECTED_FULLSCREEN_ALLOWED_BYTES,
               sizeof(EXPECTED_FULLSCREEN_ALLOWED_BYTES)) != 0) {
        /* Check if already patched to 0 */
        uint8_t patched[sizeof(EXPECTED_FULLSCREEN_ALLOWED_BYTES)];
        memcpy(patched, EXPECTED_FULLSCREEN_ALLOWED_BYTES, sizeof(patched));
        patched[6] = 0x00;

        if (memcmp(ADDR_FULLSCREEN_ALLOWED_MOV, patched, sizeof(patched)) != 0) {
            log_warning("Code mismatch at 0x%p", ADDR_FULLSCREEN_ALLOWED_MOV);
            return false;
        }
    }

    if (memcmp(ADDR_PER_STATION_WINDOWED_MOV, EXPECTED_PER_STATION_WINDOWED_BYTES,
               sizeof(EXPECTED_PER_STATION_WINDOWED_BYTES)) != 0) {
        /* Check if already patched to 1 */
        uint8_t patched[sizeof(EXPECTED_PER_STATION_WINDOWED_BYTES)];
        memcpy(patched, EXPECTED_PER_STATION_WINDOWED_BYTES, sizeof(patched));
        patched[3] = 0x01;

        if (memcmp(ADDR_PER_STATION_WINDOWED_MOV, patched, sizeof(patched)) != 0) {
            log_warning("Code mismatch at 0x%p", ADDR_PER_STATION_WINDOWED_MOV);
            return false;
        }
    }

    if (memcmp(ADDR_BACKBUFFER_FORMAT_MOV, EXPECTED_BACKBUFFER_FORMAT_BYTES,
               sizeof(EXPECTED_BACKBUFFER_FORMAT_BYTES)) != 0) {
        /* Check if already patched to 0x16 (D3DFMT_X8R8G8B8) */
        uint8_t patched[sizeof(EXPECTED_BACKBUFFER_FORMAT_BYTES)];
        memcpy(patched, EXPECTED_BACKBUFFER_FORMAT_BYTES, sizeof(patched));
        patched[4] = 0x16;

        if (memcmp(ADDR_BACKBUFFER_FORMAT_MOV, patched, sizeof(patched)) != 0) {
            log_warning("Code mismatch at 0x%p", ADDR_BACKBUFFER_FORMAT_MOV);
            return false;
        }
    }

    log_info("Target executable verified: KT_SKELETON_ST_DUAL.EXE (2012-03-07 build)");
    return true;
}

void kpm_gfx_patch_apply(bool windowed)
{
    HRESULT hr;
    uint8_t patch_allowed;
    uint8_t patch_windowed;
    uint8_t patch_format;

    if (!windowed) {
        log_info("Windowed mode patch disabled by configuration");
        return;
    }

    /* Patch mov byte ptr ds:[19BD328h], 1 -> mov byte ptr ds:[19BD328h], 0 */
    patch_allowed = 0x00;
    hr = pe_patch((void *) ((uintptr_t) ADDR_FULLSCREEN_ALLOWED_MOV + 6),
                  &patch_allowed, sizeof(patch_allowed));
    if (FAILED(hr)) {
        log_warning("Failed to patch g_FullscreenAllowed at 0x%p: hr=0x%08lx",
                    ADDR_FULLSCREEN_ALLOWED_MOV, hr);
    } else {
        log_info("Patched g_FullscreenAllowed -> 0 at 0x%p", ADDR_FULLSCREEN_ALLOWED_MOV);
    }

    /* Patch mov dword ptr [eax+20h], 0 -> mov dword ptr [eax+20h], 1 */
    patch_windowed = 0x01;
    hr = pe_patch((void *) ((uintptr_t) ADDR_PER_STATION_WINDOWED_MOV + 3),
                  &patch_windowed, sizeof(patch_windowed));
    if (FAILED(hr)) {
        log_warning("Failed to patch station Windowed at 0x%p: hr=0x%08lx",
                    ADDR_PER_STATION_WINDOWED_MOV, hr);
    } else {
        log_info("Patched station Windowed -> 1 at 0x%p", ADDR_PER_STATION_WINDOWED_MOV);
    }

    /* Patch mov dword ptr [esp+10h], 15h (D3DFMT_A8R8G8B8) -> 16h (D3DFMT_X8R8G8B8)
     * Windows 10/11 DWM windowed swapchains do not support A8R8G8B8 backbuffers for D3D9 HAL,
     * which causes CheckDeviceType to fail and falls back to REF device (causing null device crash). */
    patch_format = 0x16;
    hr = pe_patch((void *) ((uintptr_t) ADDR_BACKBUFFER_FORMAT_MOV + 4),
                  &patch_format, sizeof(patch_format));
    if (FAILED(hr)) {
        log_warning("Failed to patch backbuffer format at 0x%p: hr=0x%08lx",
                    ADDR_BACKBUFFER_FORMAT_MOV, hr);
    } else {
        log_info("Patched backbuffer format -> D3DFMT_X8R8G8B8 (0x16) at 0x%p",
                 ADDR_BACKBUFFER_FORMAT_MOV);
    }

    /* Install texture crash prevention hooks (sub_595060) */
    kpm_gfx_install_texture_crash_hook();
}

/* sub_595060 texture loader protection */
static const uintptr_t SUB_595060_ENTRY_ADDR = 0x00595060;
static const uintptr_t SUB_595060_ENTRY_CONT = 0x00595066;

static const uintptr_t SUB_595060_CRASH_ADDR = 0x005950B0;
static const uintptr_t SUB_595060_CRASH_CONT = 0x005950B7;

static void log_sub_595060_entry(void *a1, void *a2, const void *buf, uint32_t size)
{
    if (!buf || size < 4) {
        log_warning("sub_595060 load: INVALID buf=%p, size=%u", buf, size);
    }
}

static void log_sub_595060_null(void *ebx)
{
    log_warning("sub_595060: [ebx+10h] is NULL! Handled safely to prevent 0xC0000005 crash.");
}

static __declspec(naked) void sub_595060_entry_hook(void)
{
    __asm {
        pushad
        push dword ptr [esp + 32 + 16] // arg 4: size
        push dword ptr [esp + 32 + 12] // arg 3: buf
        push dword ptr [esp + 32 + 8]  // arg 2: a2
        push dword ptr [esp + 32 + 4]  // arg 1: a1
        call log_sub_595060_entry
        add esp, 16
        popad

        // Replaced bytes at 0x00595060 (6 bytes: 51 53 8B 5C 24 0C):
        push ecx
        push ebx
        mov ebx, [esp + 0x0C]
        jmp dword ptr [SUB_595060_ENTRY_CONT]
    }
}

static __declspec(naked) void sub_595060_crash_hook(void)
{
    __asm {
        mov eax, [ebx + 0x10]
        test eax, eax
        jnz normal_path

        // Null path: log warning and safely return default state
        pushad
        push ebx
        call log_sub_595060_null
        add esp, 4
        popad

        mov word ptr [ebx + 0x18], 0
        mov word ptr [ebx + 0x1A], 0
        mov word ptr [ebx + 0x38], 0
        mov dword ptr [ebx + 0x3C], 0
        pop edi
        pop esi
        pop ebp
        pop ebx
        pop ecx
        ret 0x10

    normal_path:
        mov dx, [eax + 0x0C]
        jmp dword ptr [SUB_595060_CRASH_CONT]
    }
}

static void kpm_gfx_install_texture_crash_hook(void)
{
    DWORD old_prot;
    uint8_t jmp_buf1[6];
    uint8_t jmp_buf2[7];

    /* Hook sub_595060 entry (6 bytes: 5-byte JMP + 1 NOP) */
    VirtualProtect((void *) SUB_595060_ENTRY_ADDR, 6, PAGE_EXECUTE_READWRITE, &old_prot);
    jmp_buf1[0] = 0xE9;
    *((int32_t *) &jmp_buf1[1]) = (int32_t) ((uintptr_t) sub_595060_entry_hook - (SUB_595060_ENTRY_ADDR + 5));
    jmp_buf1[5] = 0x90;
    memcpy((void *) SUB_595060_ENTRY_ADDR, jmp_buf1, 6);
    VirtualProtect((void *) SUB_595060_ENTRY_ADDR, 6, old_prot, &old_prot);

    /* Hook sub_595060 crash site (7 bytes: 5-byte JMP + 2 NOPs) */
    VirtualProtect((void *) SUB_595060_CRASH_ADDR, 7, PAGE_EXECUTE_READWRITE, &old_prot);
    jmp_buf2[0] = 0xE9;
    *((int32_t *) &jmp_buf2[1]) = (int32_t) ((uintptr_t) sub_595060_crash_hook - (SUB_595060_CRASH_ADDR + 5));
    jmp_buf2[5] = 0x90;
    jmp_buf2[6] = 0x90;
    memcpy((void *) SUB_595060_CRASH_ADDR, jmp_buf2, 7);
    VirtualProtect((void *) SUB_595060_CRASH_ADDR, 7, old_prot, &old_prot);

    log_info("Installed texture crash prevention hooks (sub_595060 entry & crash site)");

    /* Install station count safety hook */
    kpm_station_install_safety_hook();
}

/* sub_4C7580 station safety hook (handles station=1 where station[1] is NULL) */
static const uintptr_t SUB_4C7621_ADDR = 0x004C7621;
static const uintptr_t SUB_4C7621_CONT = 0x004C762C;

static __declspec(naked) void sub_4C7621_hook(void)
{
    __asm {
        test ecx, ecx
        jz null_station
        mov edx, [ecx]
        mov eax, [edx + 0x7C]
        call eax
        mov [esi + edi*4 + 0x20], eax
        jmp cont

    null_station:
        mov dword ptr [esi + edi*4 + 0x20], 0

    cont:
        jmp dword ptr [SUB_4C7621_CONT]
    }
}

static void kpm_station_install_safety_hook(void)
{
    DWORD old_prot;
    uint8_t jmp_buf[11];

    /* 11 bytes at 0x004C7621: 5-byte JMP + 6 NOPs */
    VirtualProtect((void *) SUB_4C7621_ADDR, 11, PAGE_EXECUTE_READWRITE, &old_prot);
    jmp_buf[0] = 0xE9;
    *((int32_t *) &jmp_buf[1]) = (int32_t) ((uintptr_t) sub_4C7621_hook - (SUB_4C7621_ADDR + 5));
    memset(&jmp_buf[5], 0x90, 6);
    memcpy((void *) SUB_4C7621_ADDR, jmp_buf, 11);
    VirtualProtect((void *) SUB_4C7621_ADDR, 11, old_prot, &old_prot);

    log_info("Installed station count safety hook at 0x004C7621 (sub_4C7580)");
}
