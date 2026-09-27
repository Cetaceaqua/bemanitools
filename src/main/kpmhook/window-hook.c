#define LOG_MODULE "kpm-win"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "hook/table.h"
#include "kpmhook/config-kpm.h"
#include "kpmhook/window-hook.h"
#include "kpmhook/io-hook.h"
#include "kpmhook/d3d9-hook.h"
#include "util/defs.h"
#include "util/log.h"

static bool s_windowed = true;
static HWND s_station0_hwnd = NULL;
static HWND s_station1_hwnd = NULL;

static void enable_dpi_awareness(void)
{
    static bool s_dpi_enabled = false;
    if (s_dpi_enabled) return;
    s_dpi_enabled = true;

    typedef BOOL (WINAPI *pfn_SetProcessDpiAwarenessContext)(HANDLE);
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32) {
        pfn_SetProcessDpiAwarenessContext pSetContext =
            (pfn_SetProcessDpiAwarenessContext) GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (pSetContext) {
            pSetContext((HANDLE) -4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            log_info("DPI awareness set to Per-Monitor V2");
            return;
        }
    }
    SetProcessDPIAware();
    log_info("DPI awareness set to system DPI aware");
}

struct kpm_mon_info {
    HMONITOR hmon;
    RECT rc;
    bool is_primary;
};

static struct kpm_mon_info s_mons[8];
static int s_mon_count = 0;

static BOOL CALLBACK mon_enum_cb(HMONITOR hMon, HDC hdc, LPRECT lprc, LPARAM dwData)
{
    if (s_mon_count >= 8) return TRUE;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        s_mons[s_mon_count].hmon = hMon;
        s_mons[s_mon_count].rc = mi.rcMonitor;
        s_mons[s_mon_count].is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        s_mon_count++;
    }
    return TRUE;
}

static void query_monitors(RECT *rc_main, RECT *rc_sub)
{
    enable_dpi_awareness();
    s_mon_count = 0;
    EnumDisplayMonitors(NULL, NULL, mon_enum_cb, 0);

    /* Default fallback: primary monitor or virtual screen */
    rc_main->left = 0;
    rc_main->top = 0;
    rc_main->right = GetSystemMetrics(SM_CXSCREEN);
    rc_main->bottom = GetSystemMetrics(SM_CYSCREEN);
    *rc_sub = *rc_main;

    /* Station 0 gets primary monitor */
    int primary_idx = -1;
    for (int i = 0; i < s_mon_count; i++) {
        if (s_mons[i].is_primary) {
            *rc_main = s_mons[i].rc;
            primary_idx = i;
            break;
        }
    }

    /* Station 1 gets the first non-primary monitor */
    bool found_sub = false;
    for (int i = 0; i < s_mon_count; i++) {
        if (i != primary_idx) {
            *rc_sub = s_mons[i].rc;
            found_sub = true;
            break;
        }
    }

    if (!found_sub) {
        /* Single monitor setup: place sub-screen offscreen so it never overlaps main screen */
        rc_sub->left = -10000;
        rc_sub->top = -10000;
        rc_sub->right = -9999;
        rc_sub->bottom = -9999;
    }

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        log_info("Detected %d monitor(s): Main=[%ld,%ld,%ld,%ld] Sub=[%ld,%ld,%ld,%ld]",
                 s_mon_count,
                 rc_main->left, rc_main->top, rc_main->right, rc_main->bottom,
                 rc_sub->left, rc_sub->top, rc_sub->right, rc_sub->bottom);
    }
}

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

static DWORD WINAPI exit_watchdog_thread(void *param)
{
    /* If the process has not terminated after 5 seconds of WM_CLOSE processing, force exit */
    Sleep(5000);
    log_warning("Process shutdown watchdog timed out after 5 seconds, forcing ExitProcess(0)...");
    ExitProcess(0);
    return 0;
}

static void trigger_exit_watchdog(void)
{
    static bool s_watchdog_started = false;
    if (!s_watchdog_started) {
        s_watchdog_started = true;
        kpm_io_hook_flush_nvram();
        HANDLE hThread = CreateThread(NULL, 0, exit_watchdog_thread, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
}

#include "kpmhook/touch-hook.h"

static int (WINAPI *real_ShowCursor)(BOOL bShow);

static int WINAPI my_ShowCursor(BOOL bShow)
{
    /* Always keep mouse cursor visible for PC pair-programming / play */
    if (real_ShowCursor) {
        return real_ShowCursor(TRUE);
    }
    return 1;
}

static LRESULT CALLBACK kpm_wnd_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC orig_proc = (WNDPROC) GetPropW(hWnd, L"KPM_ORIG_WNDPROC");
    if (!orig_proc) {
        orig_proc = DefWindowProcW;
    }

    switch (Msg) {
        /* Forward mouse events to Elo touch virtualizer ONLY for Station 0 (Main Screen) */
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP: {
            if (hWnd == s_station0_hwnd) {
                int type = (Msg == WM_LBUTTONDOWN) ? 1 : ((Msg == WM_MOUSEMOVE) ? 2 : 4);
                if (type == 2 && !(wParam & MK_LBUTTON)) {
                    break;
                }

                RECT dest_rc;
                int ox = 0, oy = 0, cw, ch;
                if (kpm_d3d9_get_station0_dest_rect(&dest_rc)) {
                    ox = dest_rc.left;
                    oy = dest_rc.top;
                    cw = dest_rc.right - dest_rc.left;
                    ch = dest_rc.bottom - dest_rc.top;
                } else {
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    cw = rc.right - rc.left;
                    ch = rc.bottom - rc.top;
                }

                int mx = (short) LOWORD(lParam);
                int my = (short) HIWORD(lParam);

                int rel_x = mx - ox;
                int rel_y = my - oy;
                if (rel_x < 0) rel_x = 0;
                if (rel_x > cw) rel_x = cw;
                if (rel_y < 0) rel_y = 0;
                if (rel_y > ch) rel_y = ch;

                if (type == 1) SetCapture(hWnd);
                if (type == 4) ReleaseCapture();

                kpm_touch_post_event(0, rel_x, rel_y, cw, ch, type);
            }
            break;
        }

        /* Suppress background erasing to avoid white flashes or border artifacts */
        case WM_ERASEBKGND:
            return 1;

        /* Suppress non-client painting in fullscreen to remove any Windows 11 border */
        case WM_NCPAINT:
            if (!s_windowed) return 0;
            break;

        /* Ensure 0-pixel non-client area in fullscreen mode */
        case WM_NCCALCSIZE:
            if (!s_windowed && wParam) {
                return 0;
            }
            break;

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
            log_info("Window 0x%p received WM_CLOSE, triggering NVRAM flush and clean shutdown...", hWnd);
            trigger_exit_watchdog();
            if (hWnd != s_station0_hwnd && s_station0_hwnd != NULL) {
                /* If Station 1 close button was clicked, also notify main Station 0 window */
                PostMessageW(s_station0_hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }

        case WM_DESTROY: {
            trigger_exit_watchdog();
            break;
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

    /* Force black background brush to prevent white flashes or borders */
    SetClassLongPtrW(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR) GetStockObject(BLACK_BRUSH));

    /* Disable Windows 11 borders and rounded corners */
    HMODULE dwm = GetModuleHandleA("dwmapi.dll");
    if (!dwm) dwm = LoadLibraryA("dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI *pfn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfn_DwmSetWindowAttribute pDwm = (pfn_DwmSetWindowAttribute) GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (pDwm) {
            DWORD corner = 1; /* DWMWCP_DONOTROUND */
            pDwm(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
            COLORREF border = 0xFFFFFFFE; /* DWMWA_COLOR_NONE */
            pDwm(hWnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
        }
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

        lpWindowName = L"KPM Main Screen";

        if (s_windowed) {
            /* Give Station 0 full caption, system menu, minimize button, and clean border */
            dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
            dwExStyle &= ~WS_EX_TOPMOST;

            /* Station 0 client area is always native 1024x768 landscape */
            int cw = 1024;
            int ch = 768;
            RECT r = {0, 0, cw, ch};
            AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle);
            X = 40;
            Y = 40;
            nWidth = r.right - r.left;
            nHeight = r.bottom - r.top;
        } else {
            /* Fullscreen borderless on Primary Monitor */
            RECT rc_main, rc_sub;
            query_monitors(&rc_main, &rc_sub);

            dwStyle = WS_POPUP | WS_VISIBLE;
            dwExStyle = 0; /* Strip all window borders and extended edge styles */
            X = rc_main.left;
            Y = rc_main.top;
            nWidth = rc_main.right - rc_main.left;
            nHeight = rc_main.bottom - rc_main.top;
            log_info("Station 0 fullscreen bounds: (%d, %d, %d, %d)", X, Y, nWidth, nHeight);
        }
    } else if (is_sub) {
        log_info("CreateWindowExW: Station 1 (Sub Screen) orig style=0x%08lx ex=0x%08lx size=%dx%d",
                 dwStyle, dwExStyle, nWidth, nHeight);

        if (!kpm_config_get_sub_screen()) {
            log_info("CreateWindowExW: Station 1 disabled by configuration (gfx.sub_screen=false)");
            /* Create hidden dummy window offscreen for Direct3D9 initialization requirements */
            HWND hWnd = real_CreateWindowExW(
                0, lpClassName, L"KPM Sub Screen (Hidden)", WS_POPUP,
                -10000, -10000, 1, 1, hWndParent, hMenu, hInstance, lpParam);
            if (hWnd) {
                s_station1_hwnd = hWnd;
                ShowWindow(hWnd, SW_HIDE);
            }
            return hWnd;
        }

        lpWindowName = L"KPM Sub Screen";

        if (s_windowed) {
            /* Give Station 1 also a full window and place it side-by-side next to Station 0 */
            dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
            dwExStyle &= ~WS_EX_TOPMOST;

            /* Station 1 is portrait 768x1024 if rotate enabled, else 1024x768 */
            int cw = kpm_config_get_rotate() ? 768 : 1024;
            int ch = kpm_config_get_rotate() ? 1024 : 768;
            RECT r = {0, 0, cw, ch};
            AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle);
            X = 1100;
            Y = 40;
            nWidth = r.right - r.left;
            nHeight = r.bottom - r.top;
        } else {
            /* Fullscreen borderless on Secondary Monitor */
            RECT rc_main, rc_sub;
            query_monitors(&rc_main, &rc_sub);

            dwStyle = WS_POPUP | WS_VISIBLE;
            dwExStyle = 0; /* Strip all window borders and extended edge styles */
            X = rc_sub.left;
            Y = rc_sub.top;
            nWidth = rc_sub.right - rc_sub.left;
            nHeight = rc_sub.bottom - rc_sub.top;
            log_info("Station 1 fullscreen bounds: (%d, %d, %d, %d)", X, Y, nWidth, nHeight);
        }
    }

    HWND hWnd = real_CreateWindowExW(
        dwExStyle, lpClassName, lpWindowName, dwStyle,
        X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (hWnd) {
        if (is_main) {
            s_station0_hwnd = hWnd;
            subclass_game_window(hWnd, "Station 0");
            if (!s_windowed) {
                RECT rc_main, rc_sub;
                query_monitors(&rc_main, &rc_sub);
                real_SetWindowPos(hWnd, HWND_TOP, rc_main.left, rc_main.top,
                                  rc_main.right - rc_main.left, rc_main.bottom - rc_main.top,
                                  SWP_SHOWWINDOW);
            }
        } else if (is_sub) {
            s_station1_hwnd = hWnd;
            subclass_game_window(hWnd, "Station 1");
            if (!s_windowed) {
                RECT rc_main, rc_sub;
                query_monitors(&rc_main, &rc_sub);
                real_SetWindowPos(hWnd, HWND_TOP, rc_sub.left, rc_sub.top,
                                  rc_sub.right - rc_sub.left, rc_sub.bottom - rc_sub.top,
                                  SWP_SHOWWINDOW);
            }
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

    if (hWnd == s_station1_hwnd && !kpm_config_get_sub_screen()) {
        return real_SetWindowPos(hWnd, HWND_BOTTOM, -10000, -10000, 1, 1, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }

    if (!s_windowed) {
        if (hWnd == s_station0_hwnd) {
            RECT rc_main, rc_sub;
            query_monitors(&rc_main, &rc_sub);
            X = rc_main.left;
            Y = rc_main.top;
            cx = rc_main.right - rc_main.left;
            cy = rc_main.bottom - rc_main.top;
            uFlags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            return real_SetWindowPos(hWnd, HWND_TOP, X, Y, cx, cy, uFlags | SWP_SHOWWINDOW);
        } else if (hWnd == s_station1_hwnd) {
            RECT rc_main, rc_sub;
            query_monitors(&rc_main, &rc_sub);
            X = rc_sub.left;
            Y = rc_sub.top;
            cx = rc_sub.right - rc_sub.left;
            cy = rc_sub.bottom - rc_sub.top;
            uFlags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            return real_SetWindowPos(hWnd, HWND_TOP, X, Y, cx, cy, uFlags | SWP_SHOWWINDOW);
        }
    } else {
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
            if (s_windowed) {
                /* Keep caption, sysmenu, and minimize box */
                dwNewLong |= (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
                dwNewLong &= ~WS_POPUP;
            } else {
                /* Keep borderless popup style in fullscreen mode */
                dwNewLong = (dwNewLong | WS_POPUP | WS_VISIBLE) &
                            ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            }
        } else if (nIndex == GWL_EXSTYLE) {
            /* Never allow topmost */
            dwNewLong &= ~WS_EX_TOPMOST;
        }
    }

    return real_SetWindowLongW(hWnd, nIndex, dwNewLong);
}

static BOOL (WINAPI *real_GetCursorPos)(LPPOINT lpPoint);

static BOOL WINAPI my_GetCursorPos(LPPOINT lpPoint)
{
    if (!real_GetCursorPos) {
        return FALSE;
    }

    BOOL res = real_GetCursorPos(lpPoint);
    if (res && lpPoint && s_station0_hwnd) {
        /* Convert virtual desktop screen coordinates to Station 0 client coordinates */
        ScreenToClient(s_station0_hwnd, lpPoint);

        RECT dest_rc;
        int ox = 0, oy = 0, cw, ch;
        if (kpm_d3d9_get_station0_dest_rect(&dest_rc)) {
            ox = dest_rc.left;
            oy = dest_rc.top;
            cw = dest_rc.right - dest_rc.left;
            ch = dest_rc.bottom - dest_rc.top;
        } else {
            RECT rc;
            GetClientRect(s_station0_hwnd, &rc);
            cw = rc.right - rc.left;
            ch = rc.bottom - rc.top;
        }

        /* Scale client coordinates relative to 4:3 viewport to Station 0 native resolution 1024x768 */
        if (cw > 0 && ch > 0) {
            lpPoint->x = (LONG) (((int64_t) (lpPoint->x - ox) * 1024) / cw);
            lpPoint->y = (LONG) (((int64_t) (lpPoint->y - oy) * 768) / ch);
        }

        if (lpPoint->x < 0) {
            lpPoint->x = 0;
        } else if (lpPoint->x > 1023) {
            lpPoint->x = 1023;
        }

        if (lpPoint->y < 0) {
            lpPoint->y = 0;
        } else if (lpPoint->y > 767) {
            lpPoint->y = 767;
        }
    }

    return res;
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
    {
        .name = "GetCursorPos",
        .patch = my_GetCursorPos,
        .link = (void **) &real_GetCursorPos,
    },
    {
        .name = "ShowCursor",
        .patch = my_ShowCursor,
        .link = (void **) &real_ShowCursor,
    },
};

void kpm_window_hook_init(bool windowed)
{
    s_windowed = windowed;
    enable_dpi_awareness();

    hook_table_apply(
        NULL,
        "user32.dll",
        kpm_window_syms,
        lengthof(kpm_window_syms));

    log_info("Window management hooks installed (mode=%s, DPI-aware)",
             windowed ? "windowed" : "borderless fullscreen");
}
