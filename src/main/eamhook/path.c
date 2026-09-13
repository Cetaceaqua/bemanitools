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
static BOOL (WINAPI *real_RemoveDirectoryA)(LPCSTR);
static BOOL (WINAPI *real_RemoveDirectoryW)(LPCWSTR);
static DWORD (WINAPI *real_GetFileAttributesA)(LPCSTR);
static DWORD (WINAPI *real_GetFileAttributesW)(LPCWSTR);
static BOOL (WINAPI *real_GetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL (WINAPI *real_GetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static HANDLE (WINAPI *real_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
static HANDLE (WINAPI *real_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static BOOL (WINAPI *real_DeleteFileA)(LPCSTR);
static BOOL (WINAPI *real_DeleteFileW)(LPCWSTR);
static BOOL (WINAPI *real_MoveFileA)(LPCSTR, LPCSTR);
static BOOL (WINAPI *real_MoveFileW)(LPCWSTR, LPCWSTR);
static BOOL (WINAPI *real_MoveFileExA)(LPCSTR, LPCSTR, DWORD);
static BOOL (WINAPI *real_MoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
static BOOL (WINAPI *real_SetCurrentDirectoryA)(LPCSTR);
static BOOL (WINAPI *real_SetCurrentDirectoryW)(LPCWSTR);

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

static void ensure_parent_dir_a(const char *path)
{
    char dir[MAX_PATH];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last = strrchr(dir, '\\');
    if (!last) last = strrchr(dir, '/');
    if (last) {
        *last = '\0';
        ensure_dir_a(dir);
    }
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
    if (!clean[0]) return false;

    /* If path already starts with game root dev directory, do not modify */
    char dev_root[MAX_PATH];
    snprintf(dev_root, sizeof(dev_root), "%s\\dev", s_root_a);
    size_t dev_len = strlen(dev_root);
    if (_strnicmp(clean, dev_root, dev_len) == 0 &&
        (clean[dev_len] == '\0' || clean[dev_len] == '\\' || clean[dev_len] == '/')) {
        return false;
    }

    /* E:\ redirect to <game_root>\dev\e\... */
    if ((clean[0] == 'e' || clean[0] == 'E') && clean[1] == ':' && (clean[2] == '\\' || clean[2] == '/')) {
        snprintf(out, out_len, "%s\\dev\\e\\%s", s_root_a, clean + 3);
        return true;
    }

    /* D:\KPM redirect to <game_root>\... (or dev\ if arcade storage) */
    if (_strnicmp(clean, "d:/kpm", 6) == 0 || _strnicmp(clean, "d:\\kpm", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        if (_strnicmp(rem, "avs_backup", 10) == 0 ||
            _strnicmp(rem, "raw", 3) == 0 ||
            _strnicmp(rem, "nvram", 5) == 0) {
            snprintf(out, out_len, "%s\\dev\\%s", s_root_a, rem);
            return true;
        }
        snprintf(out, out_len, "%s\\%s", s_root_a, rem);
        return true;
    }

    /* Strip drive letter if present (e.g. C:, D:) */
    const char *s = clean;
    if (((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')) && s[1] == ':') {
        s += 2;
    }
    while (*s == '\\' || *s == '/') s++;

    /* Strip leading relative prefixes (..\ or .\) */
    while (1) {
        if (s[0] == '.' && (s[1] == '\\' || s[1] == '/')) {
            s += 2;
            while (*s == '\\' || *s == '/') s++;
        } else if (s[0] == '.' && s[1] == '.' && (s[2] == '\\' || s[2] == '/')) {
            s += 3;
            while (*s == '\\' || *s == '/') s++;
        } else if (s[0] == '.' && s[1] == '.' && s[2] == '\0') {
            s += 2;
            break;
        } else {
            break;
        }
    }

    /* Check special arcade folders -> route to dev\... */
    if (_strnicmp(s, "avs_backup", 10) == 0 && (s[10] == '\0' || s[10] == '\\' || s[10] == '/')) {
        const char *rem = s + 10;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\avs_backup\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\avs_backup", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "raw", 3) == 0 && (s[3] == '\0' || s[3] == '\\' || s[3] == '/')) {
        const char *rem = s + 3;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\RAW\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\RAW", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "nvram", 5) == 0 && (s[5] == '\0' || s[5] == '\\' || s[5] == '/')) {
        const char *rem = s + 5;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\NVRAM\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\NVRAM", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "backup", 6) == 0 && (s[6] == '\0' || s[6] == '\\' || s[6] == '/')) {
        const char *rem = s + 6;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\e\\backup\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\e\\backup", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "log", 3) == 0 && (s[3] == '\0' || s[3] == '\\' || s[3] == '/')) {
        const char *rem = s + 3;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\e\\log\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\e\\log", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "download", 8) == 0 && (s[8] == '\0' || s[8] == '\\' || s[8] == '/')) {
        const char *rem = s + 8;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\e\\download\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\e\\download", s_root_a);
        }
        return true;
    }

    if (_strnicmp(s, "temp", 4) == 0 && (s[4] == '\0' || s[4] == '\\' || s[4] == '/')) {
        const char *rem = s + 4;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            snprintf(out, out_len, "%s\\dev\\e\\temp\\%s", s_root_a, rem);
        } else {
            snprintf(out, out_len, "%s\\dev\\e\\temp", s_root_a);
        }
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
        ensure_parent_dir_a(rewrote);
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
        ensure_parent_dir_a(dir_a);
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

static BOOL WINAPI my_RemoveDirectoryA(LPCSTR lpPathName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, sizeof(rewrote))) {
        return real_RemoveDirectoryA(rewrote);
    }
    return real_RemoveDirectoryA(lpPathName);
}

static BOOL WINAPI my_RemoveDirectoryW(LPCWSTR lpPathName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, lengthof(rewrote))) {
        return real_RemoveDirectoryW(rewrote);
    }
    return real_RemoveDirectoryW(lpPathName);
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

static BOOL WINAPI my_GetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, sizeof(rewrote))) {
        return real_GetFileAttributesExA(rewrote, fInfoLevelId, lpFileInformation);
    }
    return real_GetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
}

static BOOL WINAPI my_GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, lengthof(rewrote))) {
        return real_GetFileAttributesExW(rewrote, fInfoLevelId, lpFileInformation);
    }
    return real_GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
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

static BOOL WINAPI my_MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
    char r_exist[MAX_PATH];
    char r_new[MAX_PATH];
    const char *p_exist = lpExistingFileName;
    const char *p_new = lpNewFileName;

    if (rewrite_path_a(lpExistingFileName, r_exist, sizeof(r_exist))) {
        p_exist = r_exist;
    }
    if (rewrite_path_a(lpNewFileName, r_new, sizeof(r_new))) {
        ensure_parent_dir_a(r_new);
        p_new = r_new;
    }
    return real_MoveFileA(p_exist, p_new);
}

static BOOL WINAPI my_MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName)
{
    wchar_t r_exist[MAX_PATH];
    wchar_t r_new[MAX_PATH];
    const wchar_t *p_exist = lpExistingFileName;
    const wchar_t *p_new = lpNewFileName;

    if (rewrite_path_w(lpExistingFileName, r_exist, lengthof(r_exist))) {
        p_exist = r_exist;
    }
    if (rewrite_path_w(lpNewFileName, r_new, lengthof(r_new))) {
        char dir_a[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, r_new, -1, dir_a, sizeof(dir_a), NULL, NULL);
        ensure_parent_dir_a(dir_a);
        p_new = r_new;
    }
    return real_MoveFileW(p_exist, p_new);
}

static BOOL WINAPI my_MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags)
{
    char r_exist[MAX_PATH];
    char r_new[MAX_PATH];
    const char *p_exist = lpExistingFileName;
    const char *p_new = lpNewFileName;

    if (rewrite_path_a(lpExistingFileName, r_exist, sizeof(r_exist))) {
        p_exist = r_exist;
    }
    if (rewrite_path_a(lpNewFileName, r_new, sizeof(r_new))) {
        ensure_parent_dir_a(r_new);
        p_new = r_new;
    }
    return real_MoveFileExA(p_exist, p_new, dwFlags);
}

static BOOL WINAPI my_MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)
{
    wchar_t r_exist[MAX_PATH];
    wchar_t r_new[MAX_PATH];
    const wchar_t *p_exist = lpExistingFileName;
    const wchar_t *p_new = lpNewFileName;

    if (rewrite_path_w(lpExistingFileName, r_exist, lengthof(r_exist))) {
        p_exist = r_exist;
    }
    if (rewrite_path_w(lpNewFileName, r_new, lengthof(r_new))) {
        char dir_a[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, r_new, -1, dir_a, sizeof(dir_a), NULL, NULL);
        ensure_parent_dir_a(dir_a);
        p_new = r_new;
    }
    return real_MoveFileExW(p_exist, p_new, dwFlags);
}

static BOOL WINAPI my_SetCurrentDirectoryA(LPCSTR lpPathName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, sizeof(rewrote))) {
        return real_SetCurrentDirectoryA(rewrote);
    }

    /* Check if target directory escapes s_root_a (e.g. "..", "..\") */
    char full[MAX_PATH];
    if (GetFullPathNameA(lpPathName, sizeof(full), full, NULL)) {
        size_t root_len = strlen(s_root_a);
        if (_strnicmp(full, s_root_a, root_len) != 0 ||
            (full[root_len] != '\0' && full[root_len] != '\\' && full[root_len] != '/')) {
            log_info("Blocked SetCurrentDirectoryA outside root ('%s' -> '%s'), pinning to '%s'",
                     lpPathName, full, s_root_a);
            return real_SetCurrentDirectoryA(s_root_a);
        }
    }
    return real_SetCurrentDirectoryA(lpPathName);
}

static BOOL WINAPI my_SetCurrentDirectoryW(LPCWSTR lpPathName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, lengthof(rewrote))) {
        return real_SetCurrentDirectoryW(rewrote);
    }

    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(lpPathName, lengthof(full), full, NULL)) {
        size_t root_len = wcslen(s_root_w);
        if (_wcsnicmp(full, s_root_w, root_len) != 0 ||
            (full[root_len] != L'\0' && full[root_len] != L'\\' && full[root_len] != L'/')) {
            return real_SetCurrentDirectoryW(s_root_w);
        }
    }
    return real_SetCurrentDirectoryW(lpPathName);
}

static const struct hook_symbol eamhook_path_syms[] = {
    { .name = "CreateFileA",          .patch = my_CreateFileA,          .link = (void **) &real_CreateFileA },
    { .name = "CreateFileW",          .patch = my_CreateFileW,          .link = (void **) &real_CreateFileW },
    { .name = "CreateDirectoryA",     .patch = my_CreateDirectoryA,     .link = (void **) &real_CreateDirectoryA },
    { .name = "CreateDirectoryW",     .patch = my_CreateDirectoryW,     .link = (void **) &real_CreateDirectoryW },
    { .name = "RemoveDirectoryA",     .patch = my_RemoveDirectoryA,     .link = (void **) &real_RemoveDirectoryA },
    { .name = "RemoveDirectoryW",     .patch = my_RemoveDirectoryW,     .link = (void **) &real_RemoveDirectoryW },
    { .name = "GetFileAttributesA",   .patch = my_GetFileAttributesA,   .link = (void **) &real_GetFileAttributesA },
    { .name = "GetFileAttributesW",   .patch = my_GetFileAttributesW,   .link = (void **) &real_GetFileAttributesW },
    { .name = "GetFileAttributesExA", .patch = my_GetFileAttributesExA, .link = (void **) &real_GetFileAttributesExA },
    { .name = "GetFileAttributesExW", .patch = my_GetFileAttributesExW, .link = (void **) &real_GetFileAttributesExW },
    { .name = "FindFirstFileA",       .patch = my_FindFirstFileA,       .link = (void **) &real_FindFirstFileA },
    { .name = "FindFirstFileW",       .patch = my_FindFirstFileW,       .link = (void **) &real_FindFirstFileW },
    { .name = "DeleteFileA",          .patch = my_DeleteFileA,          .link = (void **) &real_DeleteFileA },
    { .name = "DeleteFileW",          .patch = my_DeleteFileW,          .link = (void **) &real_DeleteFileW },
    { .name = "MoveFileA",            .patch = my_MoveFileA,            .link = (void **) &real_MoveFileA },
    { .name = "MoveFileW",            .patch = my_MoveFileW,            .link = (void **) &real_MoveFileW },
    { .name = "MoveFileExA",          .patch = my_MoveFileExA,          .link = (void **) &real_MoveFileExA },
    { .name = "MoveFileExW",          .patch = my_MoveFileExW,          .link = (void **) &real_MoveFileExW },
    { .name = "SetCurrentDirectoryA", .patch = my_SetCurrentDirectoryA, .link = (void **) &real_SetCurrentDirectoryA },
    { .name = "SetCurrentDirectoryW", .patch = my_SetCurrentDirectoryW, .link = (void **) &real_SetCurrentDirectoryW },
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

    /* If eamhook.dll is located in eamuse subdirectory, strip \eamuse */
    size_t slen = strlen(s_root_a);
    while (slen > 0 && (s_root_a[slen - 1] == '\\' || s_root_a[slen - 1] == '/')) {
        s_root_a[--slen] = '\0';
    }
    if (slen >= 7 && (_stricmp(s_root_a + slen - 7, "\\eamuse") == 0 || _stricmp(s_root_a + slen - 7, "/eamuse") == 0)) {
        s_root_a[slen - 7] = '\0';
    }

    MultiByteToWideChar(CP_ACP, 0, s_root_a, -1, s_root_w, lengthof(s_root_w));
    log_info("eamhook game root resolved to: %s", s_root_a);

    /* Pre-create required directory structure under dev\ */
    char sub[MAX_PATH];
    snprintf(sub, sizeof(sub), "%s\\dev", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\e", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\e\\log", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\e\\backup", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\e\\download", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\e\\temp", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\avs_backup", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\RAW", s_root_a);
    ensure_dir_a(sub);
    snprintf(sub, sizeof(sub), "%s\\dev\\NVRAM", s_root_a);
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

    log_info("Installed comprehensive path redirection hooks for eam_if and network modules");
}
