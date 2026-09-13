#define LOG_MODULE "kpm-movie"

#include <windows.h>
#include <objbase.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "kpmhook/config-io.h"
#include "kpmhook/d3d9-hook.h"
#include "kpmhook/movie-hook.h"
#include "kpmhook/path-hook.h"
#include "util/log.h"


#define ADDR_VTABLE_WMV_OPEN         0x00C4E624
#define ADDR_STEP17_READY_CALL       0x004A2535
#define ADDR_CALL_STEP14_CHECK_MOVIE 0x004A243A

static bool s_play_movie = true;
static uint32_t s_step17_wait_frames = 0;

static void patch_memory(uintptr_t addr, const void *bytes, size_t len)
{
    DWORD old_protect;
    VirtualProtect((void *) addr, len, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *) addr, bytes, len);
    VirtualProtect((void *) addr, len, old_protect, &old_protect);
}

static void patch_call(uintptr_t call_site, const void *target_func)
{
    uint32_t rel = (uint32_t) ((uintptr_t) target_func - (call_site + 5));
    uint8_t call_bytes[5];
    call_bytes[0] = 0xE8;
    memcpy(&call_bytes[1], &rel, sizeof(rel));
    patch_memory(call_site, call_bytes, sizeof(call_bytes));
}

static char call_real_wmv_open(void *this_ptr, const wchar_t *pszFileName, int a3, int a4, int a5)
{
    char result;
    __asm {
        mov ecx, this_ptr
        push a5
        push a4
        push a3
        push pszFileName
        mov eax, 0x005C68E0
        call eax
        mov result, al
    }
    return result;
}

static char __cdecl my_wmv_open_impl(void *this_ptr, const wchar_t *pszFileName, int a3, int a4, int a5)
{
    /* Initialize COM apartment on the background loader thread */
    HRESULT hr_co = CoInitialize(NULL);
    if (FAILED(hr_co) && hr_co != RPC_E_CHANGED_MODE) {
        log_warning("CoInitialize failed on movie thread: 0x%08lX", hr_co);
    }

    if (!s_play_movie) {
        log_info("Movie playback disabled in config; skipping CWmvPlayerVMR9::Open");
        return 0;
    }

    if (!pszFileName) {
        log_warning("CWmvPlayerVMR9::Open called with NULL filename");
        return 0;
    }

    /* Clean up and guarantee null-termination after .wmv / .WMV */
    wchar_t clean_path[MAX_PATH];
    wcsncpy(clean_path, pszFileName, MAX_PATH - 1);
    clean_path[MAX_PATH - 1] = L'\0';

    wchar_t *dot = wcsstr(clean_path, L".wmv");
    if (!dot) {
        dot = wcsstr(clean_path, L".WMV");
    }
    if (dot) {
        dot[4] = L'\0';
    }

    /* Rewrite path from D:\KPM\data\movie to local game directory */
    wchar_t rewritten_path[MAX_PATH];
    const wchar_t *final_path = clean_path;
    if (kpm_path_rewrite_w(clean_path, rewritten_path, MAX_PATH, false)) {
        final_path = rewritten_path;
    }

    DWORD attr = GetFileAttributesW(final_path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        log_warning("CWmvPlayerVMR9::Open: movie file not found: '%ls'", final_path);
        return 0;
    }

    /* DirectShow VMR9 needs the dev_proxy (which intercepts IDirect3DVideoDevice9 to enable
     * DXVK / software fallback), and the genuine IDirect3D9 pointer.
     */
    IDirect3D9 *real_d3d9 = kpm_d3d9_get_real_d3d9();
    IDirect3DDevice9 *dev_proxy = kpm_d3d9_get_device_proxy();
    IDirect3DDevice9 *real_dev = kpm_d3d9_get_real_device();
    int actual_a3 = real_d3d9 ? (int) real_d3d9 : a3;
    int actual_a4 = dev_proxy ? (int) dev_proxy : (real_dev ? (int) real_dev : a4);

    log_info("CWmvPlayerVMR9::Open: opening '%ls' (this=%p, d3d9=%p, dev=%p)",
             final_path, this_ptr, (void *) actual_a3, (void *) actual_a4);
    char res = call_real_wmv_open(this_ptr, final_path, actual_a3, actual_a4, a5);
    uint32_t err_step = *(uint32_t *)((uint8_t *)this_ptr + 76);
    uint32_t hr_err = *(uint32_t *)((uint8_t *)this_ptr + 80);
    log_info("CWmvPlayerVMR9::Open returned: %d (error_step=%u, hr=0x%08X)", res, err_step, hr_err);
    return res;
}


static __declspec(naked) void my_wmv_open_thunk(void)
{
    __asm {
        push ebp
        mov ebp, esp
        push [ebp + 20] // a5
        push [ebp + 16] // a4
        push [ebp + 12] // a3
        push [ebp + 8]  // pszFileName
        push ecx        // this_ptr
        call my_wmv_open_impl
        add esp, 20
        mov esp, ebp
        pop ebp
        ret 16
    }
}

static uint8_t call_is_playing(void *this_ptr)
{
    uint8_t res = 0;
    uint32_t *vtable = *(uint32_t **)this_ptr;
    uint32_t fn;

    if (!vtable) return 0;
    fn = vtable[9]; // index 9 = offset 36 (0x24)
    __asm {
        mov ecx, this_ptr
        call fn
        mov res, al
    }
    return res;
}

static char __cdecl my_check_movie_ready(uint8_t *movie_ctrl)
{
    if (!movie_ctrl) {
        return 1;
    }

    uint32_t state = *(uint32_t *)(movie_ctrl + 1744);

    if (!s_play_movie) {
        log_info("Movie disabled: advancing Step 17 directly to Slot Mode");
        *(uint32_t *)(movie_ctrl + 2064) = 0;
        s_step17_wait_frames = 0;
        return 1;
    }

    /* Error state 200: video failed to load or hardware unsupported */
    if (state == 200) {
        log_info("CMovieCtrl reached error state 200; bypassing Step 17 to advance to Slot Mode");
        *(uint32_t *)(movie_ctrl + 2064) = 0;
        s_step17_wait_frames = 0;
        return 1;
    }

    /* Normal playback check: CWmvPlayerVMR9 pointer at offset 1764 (0x6E4) */
    uint8_t *player = *(uint8_t **)(movie_ctrl + 1764);
    if (player && *(uint32_t **)player) {
        if (call_is_playing(player)) {
            uint32_t frames = *(uint32_t *)(movie_ctrl + 2064);
            frames++;
            *(uint32_t *)(movie_ctrl + 2064) = frames;
            if (frames > 4) {
                log_info("Attract movie started playing (>4 frames output); entering Step 18");
                s_step17_wait_frames = 0;
                return 1;
            }
            return 0;
        } else {
            *(uint32_t *)(movie_ctrl + 2064) = 0;
        }
    }


    /* Step 17 watchdog: timeout after ~180 frames (approx 3 seconds at 60 fps) */
    s_step17_wait_frames++;
    if (s_step17_wait_frames > 180) {
        log_warning("Step 17 watchdog timeout (>180 frames without video output); bypassing to avoid white screen hang");
        *(uint32_t *)(movie_ctrl + 2064) = 0;
        s_step17_wait_frames = 0;
        return 1;
    }

    return 0;
}

static __declspec(naked) void my_movie_ready_thunk(void)
{
    __asm {
        push ebp
        mov ebp, esp
        push esi // pass CMovieCtrl* in esi
        call my_check_movie_ready
        add esp, 4
        mov esp, ebp
        pop ebp
        ret
    }
}

static bool my_step14_check_movie(int char_idx)
{
    if (!s_play_movie) {
        log_info("Movie playback disabled (play_movie=false): skipping Step 15 white fadeout, going directly to slot mode");
        return false;
    }

    bool res = false;
    __asm {
        mov esi, char_idx
        mov eax, 0x00542C90
        call eax
        mov res, al
    }
    return res;
}

static __declspec(naked) void my_step14_check_movie_thunk(void)
{
    __asm {
        push esi
        call my_step14_check_movie
        add esp, 4
        ret
    }
}

void kpm_movie_hook_init(const struct kpmhook_config_io *cfg)
{
    s_play_movie = cfg ? cfg->play_movie : true;

    /* Hook CWmvPlayerVMR9 vtable slot 2 (Open) */
    uintptr_t hook_fn = (uintptr_t) my_wmv_open_thunk;
    patch_memory(ADDR_VTABLE_WMV_OPEN, &hook_fn, sizeof(hook_fn));
    log_info("Hooked CWmvPlayerVMR9::Open at vtable 0x%08X", ADDR_VTABLE_WMV_OPEN);

    /* Hook CGameTitle Step 14 check movie call site (0x004A243A) */
    patch_call(ADDR_CALL_STEP14_CHECK_MOVIE, my_step14_check_movie_thunk);
    log_info("Hooked CGameTitle Step 14 check movie at 0x%08X", ADDR_CALL_STEP14_CHECK_MOVIE);

    /* Hook CGameTitle Step 17 ready check call site (0x004A2535) */
    patch_call(ADDR_STEP17_READY_CALL, my_movie_ready_thunk);
    log_info("Hooked CGameTitle Step 17 ready check at 0x%08X", ADDR_STEP17_READY_CALL);

    log_info("Movie playback and watchdog subsystem initialized (play_movie=%d)", s_play_movie);
}
