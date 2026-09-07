#define LOG_MODULE "kpm-locale"

#include <windows.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>

#include "hook/table.h"
#include "kpmhook1/locale-hook.h"
#include "util/defs.h"
#include "util/log.h"

static int (WINAPI *real_MultiByteToWideChar)(
    UINT CodePage,
    DWORD dwFlags,
    LPCCH lpMultiByteStr,
    int cbMultiByte,
    LPWSTR lpWideCharStr,
    int cchWideChar);

static int (WINAPI *real_WideCharToMultiByte)(
    UINT CodePage,
    DWORD dwFlags,
    LPCWCH lpWideCharStr,
    int cchWideChar,
    LPSTR lpMultiByteStr,
    int cbMultiByte,
    LPCCH lpDefaultChar,
    LPBOOL lpUsedDefaultChar);

static HFONT (WINAPI *real_CreateFontW)(
    int cHeight,
    int cWidth,
    int cEscapement,
    int cOrientation,
    int cWeight,
    DWORD bItalic,
    DWORD bUnderline,
    DWORD bStrikeOut,
    DWORD iCharSet,
    DWORD iOutPrecision,
    DWORD iClipPrecision,
    DWORD iQuality,
    DWORD iPitchAndFamily,
    LPCWSTR pszFaceName);

static HANDLE (WINAPI *real_AddFontMemResourceEx)(
    PVOID pFileView,
    DWORD cjSize,
    PVOID pvResrved,
    DWORD *pNumFonts);

static int WINAPI my_MultiByteToWideChar(
    UINT CodePage,
    DWORD dwFlags,
    LPCCH lpMultiByteStr,
    int cbMultiByte,
    LPWSTR lpWideCharStr,
    int cchWideChar)
{
    /* Redirect default ANSI code pages to Shift-JIS (CP932) */
    if (CodePage == CP_ACP || CodePage == CP_THREAD_ACP) {
        CodePage = 932;
    }

    return real_MultiByteToWideChar(
        CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}

static int WINAPI my_WideCharToMultiByte(
    UINT CodePage,
    DWORD dwFlags,
    LPCWCH lpWideCharStr,
    int cchWideChar,
    LPSTR lpMultiByteStr,
    int cbMultiByte,
    LPCCH lpDefaultChar,
    LPBOOL lpUsedDefaultChar)
{
    /* Redirect default ANSI code pages to Shift-JIS (CP932) */
    if (CodePage == CP_ACP || CodePage == CP_THREAD_ACP) {
        CodePage = 932;
    }

    return real_WideCharToMultiByte(
        CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr,
        cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
}

static HFONT WINAPI my_CreateFontW(
    int cHeight,
    int cWidth,
    int cEscapement,
    int cOrientation,
    int cWeight,
    DWORD bItalic,
    DWORD bUnderline,
    DWORD bStrikeOut,
    DWORD iCharSet,
    DWORD iOutPrecision,
    DWORD iClipPrecision,
    DWORD iQuality,
    DWORD iPitchAndFamily,
    LPCWSTR pszFaceName)
{
    DWORD orig_charset = iCharSet;
    if (iCharSet == DEFAULT_CHARSET) {
        iCharSet = SHIFTJIS_CHARSET;
    }

    HFONT hFont = real_CreateFontW(
        cHeight, cWidth, cEscapement, cOrientation, cWeight,
        bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
        iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);

    log_info("CreateFontW: face='%S' h=%d charset=%u[orig=%u] -> hFont=0x%p",
             pszFaceName ? pszFaceName : L"(null)", cHeight, iCharSet, orig_charset, hFont);

    /* If font creation failed and a specific face was requested, fallback to Japanese standard font */
    if (!hFont && pszFaceName) {
        hFont = real_CreateFontW(
            cHeight, cWidth, cEscapement, cOrientation, cWeight,
            bItalic, bUnderline, bStrikeOut, SHIFTJIS_CHARSET, iOutPrecision,
            iClipPrecision, iQuality, iPitchAndFamily, L"MS Gothic");
        if (hFont) {
            log_info("CreateFontW fallback to 'MS Gothic' succeeded: 0x%p", hFont);
        }
    }

    return hFont;
}

static HANDLE WINAPI my_AddFontMemResourceEx(
    PVOID pFileView,
    DWORD cjSize,
    PVOID pvResrved,
    DWORD *pNumFonts)
{
    HANDLE h = real_AddFontMemResourceEx(pFileView, cjSize, pvResrved, pNumFonts);
    log_info("AddFontMemResourceEx(view=0x%p, size=%u) -> handle=0x%p (num_fonts=%u)",
             pFileView, cjSize, h, pNumFonts ? *pNumFonts : 0);
    return h;
}

static const struct hook_symbol kpm_kernel32_locale_syms[] = {
    {
        .name = "MultiByteToWideChar",
        .patch = my_MultiByteToWideChar,
        .link = (void **) &real_MultiByteToWideChar,
    },
    {
        .name = "WideCharToMultiByte",
        .patch = my_WideCharToMultiByte,
        .link = (void **) &real_WideCharToMultiByte,
    },
};

static const struct hook_symbol kpm_gdi32_locale_syms[] = {
    {
        .name = "CreateFontW",
        .patch = my_CreateFontW,
        .link = (void **) &real_CreateFontW,
    },
    {
        .name = "AddFontMemResourceEx",
        .patch = my_AddFontMemResourceEx,
        .link = (void **) &real_AddFontMemResourceEx,
    },
};

void kpm_locale_hook_init(void)
{
    /* Set CRT and Win32 thread locale to Japanese Shift-JIS */
    setlocale(LC_ALL, ".932");
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_DEFAULT));
    SetThreadUILanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));

    hook_table_apply(
        NULL,
        "kernel32.dll",
        kpm_kernel32_locale_syms,
        lengthof(kpm_kernel32_locale_syms));

    hook_table_apply(
        NULL,
        "gdi32.dll",
        kpm_gdi32_locale_syms,
        lengthof(kpm_gdi32_locale_syms));

    log_info("Locale and Shift-JIS charset hooks installed (CP932 redirection active)");
}
