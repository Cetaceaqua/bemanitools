#define LOG_MODULE "kpm-win"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "hook/table.h"
#include "kpmhook1/config-kpm.h"
#include "kpmhook1/window-hook.h"
#include "util/defs.h"
#include "util/log.h"

static HWND s_station0_hwnd = NULL;
static HWND s_station1_hwnd = NULL;

static void get_station_client_size(HWND hWnd, int *w, int *h)
{
    if (hWnd == s_station1_hwnd) {
        if (kpm_config_get_rotate()) {
            *w = 768;
            *h = 1024;
        } else {
            *w = 1024;
            *h = 768;
        }
    } else {
        /* Station 0 (Main Screen) is always landscape 1024x768 */
        *w = 1024;
        *h = 768;
    }
}

static HWND (WINAPI *real_CreateWindowExW)(
    DWORD dwExStyle,
    LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam);

static BOOL (WINAPI *real_SetWindowPos)(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags);

static LONG (WINAPI *real_SetWindowLongW)(
    HWND hWnd,
    int nIndex,
    LONG dwNewLong);

static LRESULT CALLBACK kpm_wnd_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC orig_proc = (WNDPROC) GetPropW(hWnd, L"KPM_ORIG_WNDPROC");
    if (!orig_proc) {
        orig_proc = DefWindowProcW;
    }

    switch (Msg) {
        /* Allow Windows to process non-client clicks for dragging, title bar buttons, menus */
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONUP:
        case WM_NCMOUSEMOVE:
            return DefWindowProcW(hWnd, Msg, wParam, lParam);

        /* Allow minimize, maximize, and catch close button */
        case WM_SYSCOMMAND: {
            UINT cmd = wParam & 0xFFF0;
            if (cmd == SC_CLOSE) {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (cmd == SC_MINIMIZE || cmd == SC_MAXIMIZE || cmd == SC_RESTORE) {
                return DefWindowProcW(hWnd, Msg, wParam, lParam);
            }
            break;
        }

        /* Clean exit on close button or Alt+F4 */
        case WM_CLOSE: {
            log_info("Window 0x%p received WM_CLOSE, exiting process cleanly...", hWnd);
            DestroyWindow(hWnd);
            PostQuitMessage(0);
            ExitProcess(0);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }

    return CallWindowProcW(orig_proc, hWnd, Msg, wParam, lParam);
}

static void subclass_game_window(HWND hWnd, const char *desc)
{
    WNDPROC old_proc = (WNDPROC) GetWindowLongPtrW(hWnd, GWLP_WNDPROC);
    if (old_proc && old_proc != kpm_wnd_proc) {
        SetPropW(hWnd, L"KPM_ORIG_WNDPROC", (HANDLE) old_proc);
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR) kpm_wnd_proc);
        log_info("Subclassed %s (hWnd=0x%p, orig=0x%p)", desc, hWnd, old_proc);
    }
}

static HWND WINAPI my_CreateWindowExW(
    DWORD dwExStyle,
    LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam)
{
    bool is_main = false;
    bool is_sub = false;

    if (s_station0_hwnd == NULL) {
        is_main = true;
    } else if (s_station1_hwnd == NULL) {
        is_sub = true;
    }

    if (is_main) {
        log_info("CreateWindowExW: Station 0 (Main Screen) orig style=0x%08lx ex=0x%08lx size=%dx%d",
                 dwStyle, dwExStyle, nWidth, nHeight);

        /* Give Station 0 full caption, system menu, minimize button, and clean border */
        dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        dwExStyle &= ~WS_EX_TOPMOST;
        lpWindowName = L"LovePlus MEDAL - Main Screen (Station 1)";

        /* Station 0 client area is always native 1024x768 landscape */
        int cw = 1024;
        int ch = 768;
        RECT r = {0, 0, cw, ch};
        AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle);
        X = 40;
        Y = 40;
        nWidth = r.right - r.left;
        nHeight = r.bottom - r.top;
    } else if (is_sub) {
        log_info("CreateWindowExW: Station 1 (Sub Screen) orig style=0x%08lx ex=0x%08lx size=%dx%d",
                 dwStyle, dwExStyle, nWidth, nHeight);

        /* Give Station 1 also a full window and place it side-by-side next to Station 0 */
        dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        dwExStyle &= ~WS_EX_TOPMOST;
        lpWindowName = L"LovePlus MEDAL - Sub Screen (Station 2)";

        /* Station 1 is portrait 768x1024 if rotate enabled, else 1024x768 */
        int cw = kpm_config_get_rotate() ? 768 : 1024;
        int ch = kpm_config_get_rotate() ? 1024 : 768;
        RECT r = {0, 0, cw, ch};
        AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle);
        X = 1100;
        Y = 40;
        nWidth = r.right - r.left;
        nHeight = r.bottom - r.top;
    }

    HWND hWnd = real_CreateWindowExW(
        dwExStyle, lpClassName, lpWindowName, dwStyle,
        X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (hWnd) {
        if (is_main) {
            s_station0_hwnd = hWnd;
            subclass_game_window(hWnd, "Station 0");
        } else if (is_sub) {
            s_station1_hwnd = hWnd;
            subclass_game_window(hWnd, "Station 1");
        }
    }

    return hWnd;
}

static BOOL WINAPI my_SetWindowPos(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags)
{
    /* Never allow topmost */
    if (hWndInsertAfter == HWND_TOPMOST) {
        hWndInsertAfter = HWND_NOTOPMOST;
    }

    if (hWnd == s_station0_hwnd || hWnd == s_station1_hwnd) {
        /* Preserve current window position instead of forcing (0, 0) */
        if (!(uFlags & SWP_NOMOVE)) {
            RECT cur;
            if (GetWindowRect(hWnd, &cur)) {
                if (X <= 0 && Y <= 0) {
                    X = cur.left;
                    Y = cur.top;
                }
            }
        }

        /* Ensure window dimensions match client area */
        if (!(uFlags & SWP_NOSIZE)) {
            DWORD style = (DWORD) GetWindowLongPtrW(hWnd, GWL_STYLE);
            DWORD ex_style = (DWORD) GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
            int cw, ch;
            get_station_client_size(hWnd, &cw, &ch);
            RECT r = {0, 0, cw, ch};
            AdjustWindowRectEx(&r, style, FALSE, ex_style);
            cx = r.right - r.left;
            cy = r.bottom - r.top;
        }
    }

    return real_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static LONG WINAPI my_SetWindowLongW(
    HWND hWnd,
    int nIndex,
    LONG dwNewLong)
{
    if (hWnd == s_station0_hwnd || hWnd == s_station1_hwnd) {
        if (nIndex == GWL_STYLE) {
            /* Keep caption, sysmenu, and minimize box */
            dwNewLong |= (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
            dwNewLong &= ~WS_POPUP;
        } else if (nIndex == GWL_EXSTYLE) {
            /* Never allow topmost */
            dwNewLong &= ~WS_EX_TOPMOST;
        }
    }

    return real_SetWindowLongW(hWnd, nIndex, dwNewLong);
}

static const struct hook_symbol kpm_window_syms[] = {
    {
        .name = "CreateWindowExW",
        .patch = my_CreateWindowExW,
        .link = (void **) &real_CreateWindowExW,
    },
    {
        .name = "SetWindowPos",
        .patch = my_SetWindowPos,
        .link = (void **) &real_SetWindowPos,
    },
    {
        .name = "SetWindowLongW",
        .patch = my_SetWindowLongW,
        .link = (void **) &real_SetWindowLongW,
    },
};

void kpm_window_hook_init(void)
{
    hook_table_apply(
        NULL,
        "user32.dll",
        kpm_window_syms,
        lengthof(kpm_window_syms));

    log_info("Window management hooks installed (dual-window layout & dragging enabled)");
}
