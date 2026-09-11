#define LOG_MODULE "eamhook-path"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

static char s_root_a[MAX_PATH];
static wchar_t s_root_w[MAX_PATH];

static HANDLE (WINAPI *real_CreateFileA)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *real_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL (WINAPI *real_CreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
static BOOL (WINAPI *real_CreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
static DWORD (WINAPI *real_GetFileAttributesA)(LPCSTR);
static DWORD (WINAPI *real_GetFileAttributesW)(LPCWSTR);
static HANDLE (WINAPI *real_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
static HANDLE (WINAPI *real_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static BOOL (WINAPI *real_DeleteFileA)(LPCSTR);
static BOOL (WINAPI *real_DeleteFileW)(LPCWSTR);

static void ensure_dir_a(const char *dir)
{
    char tmp[MAX_PATH];
    strncpy(tmp, dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static bool rewrite_path_a(const char *in, char *out, size_t out_len)
{
    if (!in || !*in) return false;

    char clean[MAX_PATH];
    const char *p = in;
    while (*p == '"' || *p == '\'') p++;
    strncpy(clean, p, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';
    size_t len = strlen(clean);
    while (len > 0 && (clean[len - 1] == '"' || clean[len - 1] == '\'')) {
        clean[--len] = '\0';
    }

    /* E:\ redirect to <game_root>\e\... */
    if (_strnicmp(clean, "e:/", 3) == 0 || _strnicmp(clean, "e:\\", 3) == 0) {
        snprintf(out, out_len, "%s\\e\\%s", s_root_a, clean + 3);
        return true;
    }

    /* D:\KPM redirect to <game_root>\... */
    if (_strnicmp(clean, "d:/kpm", 6) == 0 || _strnicmp(clean, "d:\\kpm", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\%s", s_root_a, rem);
        return true;
    }

    /* D:\avs_backup redirect to <game_root>\avs_backup\... */
    if (_strnicmp(clean, "d:/avs_backup", 13) == 0 || _strnicmp(clean, "d:\\avs_backup", 13) == 0) {
        const char *rem = clean + 13;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\avs_backup\\%s", s_root_a, rem);
        return true;
    }

    /* D:\RAW redirect to <game_root>\RAW\... */
    if (_strnicmp(clean, "d:/raw", 6) == 0 || _strnicmp(clean, "d:\\raw", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\RAW\\%s", s_root_a, rem);
        return true;
    }

    /* D:\NVRAM redirect to <game_root>\NVRAM\... */
    if (_strnicmp(clean, "d:/nvram", 8) == 0 || _strnicmp(clean, "d:\\nvram", 8) == 0) {
        const char *rem = clean + 8;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\NVRAM\\%s", s_root_a, rem);
        return true;
    }

    /* D:\backup, D:\log, D:\download, D:\temp -> <game_root>\e\... */
    if (_strnicmp(clean, "d:/backup", 9) == 0 || _strnicmp(clean, "d:\\backup", 9) == 0) {
        const char *rem = clean + 9;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\e\\backup\\%s", s_root_a, rem);
        return true;
    }
    if (_strnicmp(clean, "d:/log", 6) == 0 || _strnicmp(clean, "d:\\log", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\e\\log\\%s", s_root_a, rem);
        return true;
    }
    if (_strnicmp(clean, "d:/download", 11) == 0 || _strnicmp(clean, "d:\\download", 11) == 0) {
        const char *rem = clean + 11;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\e\\download\\%s", s_root_a, rem);
        return true;
    }
    if (_strnicmp(clean, "d:/temp", 7) == 0 || _strnicmp(clean, "d:\\temp", 7) == 0) {
        const char *rem = clean + 7;
        while (*rem == '/' || *rem == '\\') rem++;
        snprintf(out, out_len, "%s\\e\\temp\\%s", s_root_a, rem);
        return true;
    }

    return false;
}

static bool rewrite_path_w(const wchar_t *in, wchar_t *out, size_t out_len)
{
    if (!in || !*in) return false;

    char in_a[MAX_PATH];
    char out_a[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, in, -1, in_a, sizeof(in_a), NULL, NULL);

    if (rewrite_path_a(in_a, out_a, sizeof(out_a))) {
        MultiByteToWideChar(CP_ACP, 0, out_a, -1, out, (int) out_len);
        return true;
    }
    return false;
}

static HANDLE WINAPI my_CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, sizeof(rewrote))) {
        char dir[MAX_PATH];
        strncpy(dir, rewrote, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *last = strrchr(dir, '\\');
        if (!last) last = strrchr(dir, '/');
        if (last) {
            *last = '\0';
            ensure_dir_a(dir);
        }
        return real_CreateFileA(
            rewrote, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    return real_CreateFileA(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static HANDLE WINAPI my_CreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, lengthof(rewrote))) {
        char dir_a[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, rewrote, -1, dir_a, sizeof(dir_a), NULL, NULL);
        char *last = strrchr(dir_a, '\\');
        if (!last) last = strrchr(dir_a, '/');
        if (last) {
            *last = '\0';
            ensure_dir_a(dir_a);
        }
        return real_CreateFileW(
            rewrote, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    return real_CreateFileW(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static BOOL WINAPI my_CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, sizeof(rewrote))) {
        ensure_dir_a(rewrote);
        return TRUE;
    }
    return real_CreateDirectoryA(lpPathName, lpSecurityAttributes);
}

static BOOL WINAPI my_CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, lengthof(rewrote))) {
        char dir_a[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, rewrote, -1, dir_a, sizeof(dir_a), NULL, NULL);
        ensure_dir_a(dir_a);
        return TRUE;
    }
    return real_CreateDirectoryW(lpPathName, lpSecurityAttributes);
}

static DWORD WINAPI my_GetFileAttributesA(LPCSTR lpFileName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, sizeof(rewrote))) {
        return real_GetFileAttributesA(rewrote);
    }
    return real_GetFileAttributesA(lpFileName);
}

static DWORD WINAPI my_GetFileAttributesW(LPCWSTR lpFileName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, lengthof(rewrote))) {
        return real_GetFileAttributesW(rewrote);
    }
    return real_GetFileAttributesW(lpFileName);
}

static HANDLE WINAPI my_FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, sizeof(rewrote))) {
        return real_FindFirstFileA(rewrote, lpFindFileData);
    }
    return real_FindFirstFileA(lpFileName, lpFindFileData);
}

static HANDLE WINAPI my_FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, lengthof(rewrote))) {
        return real_FindFirstFileW(rewrote, lpFindFileData);
    }
    return real_FindFirstFileW(lpFileName, lpFindFileData);
}

static BOOL WINAPI my_DeleteFileA(LPCSTR lpFileName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, sizeof(rewrote))) {
        return real_DeleteFileA(rewrote);
    }
    return real_DeleteFileA(lpFileName);
}

static BOOL WINAPI my_DeleteFileW(LPCWSTR lpFileName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, lengthof(rewrote))) {
        return real_DeleteFileW(rewrote);
    }
    return real_DeleteFileW(lpFileName);
}

static const struct hook_symbol eamhook_path_syms[] = {
    { .name = "CreateFileA",         .patch = my_CreateFileA,         .link = (void **) &real_CreateFileA },
    { .name = "CreateFileW",         .patch = my_CreateFileW,         .link = (void **) &real_CreateFileW },
    { .name = "CreateDirectoryA",    .patch = my_CreateDirectoryA,    .link = (void **) &real_CreateDirectoryA },
    { .name = "CreateDirectoryW",    .patch = my_CreateDirectoryW,    .link = (void **) &real_CreateDirectoryW },
    { .name = "GetFileAttributesA",  .patch = my_GetFileAttributesA,  .link = (void **) &real_GetFileAttributesA },
    { .name = "GetFileAttributesW",  .patch = my_GetFileAttributesW,  .link = (void **) &real_GetFileAttributesW },
    { .name = "FindFirstFileA",      .patch = my_FindFirstFileA,      .link = (void **) &real_FindFirstFileA },
    { .name = "FindFirstFileW",      .patch = my_FindFirstFileW,      .link = (void **) &real_FindFirstFileW },
    { .name = "DeleteFileA",         .patch = my_DeleteFileA,         .link = (void **) &real_DeleteFileA },
    { .name = "DeleteFileW",         .patch = my_DeleteFileW,         .link = (void **) &real_DeleteFileW },
};

void eamhook_path_init(void)
{
    HMODULE h_self = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCSTR) eamhook_path_init,
        &h_self);

    if (h_self) {
        GetModuleFileNameA(h_self, s_root_a, sizeof(s_root_a));
        char *p = strrchr(s_root_a, '\\');
        if (!p) p = strrchr(s_root_a, '/');
        if (p) *p = '\0';
    } else {
        GetCurrentDirectoryA(sizeof(s_root_a), s_root_a);
    }

    MultiByteToWideChar(CP_ACP, 0, s_root_a, -1, s_root_w, lengthof(s_root_w));
    log_info("eamhook game root resolved to: %s", s_root_a);

    /* Pre-create required directory structure */
    char sub[MAX_PATH];
    snprintf(sub, sizeof(sub), "%s\\e\\log", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\e\\backup", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\e\\download", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\e\\temp", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\avs_backup", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\RAW", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\NVRAM", s_root_a);
    ensure_dir_a(sub);

    /* Apply path hooks to main exe and any loaded/dependent modules */
    hook_table_apply(
        NULL, "kernel32.dll", eamhook_path_syms, lengthof(eamhook_path_syms));

    static const char *const target_modules[] = {
        "eam3util.dll",
        "libavs-win32.dll",
        "libavs-win32-ea3.dll",
        "KPM_EAM_MOD.DLL",
        "tf_eam_mod.dll",
    };

    for (size_t i = 0; i < lengthof(target_modules); i++) {
        HMODULE h = GetModuleHandleA(target_modules[i]);
        if (h) {
            hook_table_apply(
                h, "kernel32.dll", eamhook_path_syms, lengthof(eamhook_path_syms));
        }
    }

    log_info("Installed path redirection hooks for E:/, D:/KPM/, D:/avs_backup/, D:/RAW/, D:/NVRAM/");
}
