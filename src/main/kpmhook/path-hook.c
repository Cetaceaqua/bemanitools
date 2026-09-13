#define LOG_MODULE "kpm-path"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hook/table.h"
#include "kpmhook/path-hook.h"
#include "util/defs.h"
#include "util/log.h"

static char s_game_root_a[MAX_PATH];
static wchar_t s_game_root_w[MAX_PATH];
static size_t s_game_root_a_len;
static size_t s_game_root_w_len;

static void ensure_dir_exists_a(const char *dir_path)
{
    char tmp[MAX_PATH];
    char *p;

    if (!dir_path || !*dir_path) return;
    strncpy(tmp, dir_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (p = tmp + 3; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char orig = *p;
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = orig;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static void ensure_parent_dirs_exist_a(const char *file_path)
{
    char tmp[MAX_PATH];
    char *last_sep;

    if (!file_path || !*file_path) return;
    strncpy(tmp, file_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    last_sep = strrchr(tmp, '\\');
    if (!last_sep) {
        last_sep = strrchr(tmp, '/');
    }
    if (last_sep) {
        *last_sep = '\0';
        ensure_dir_exists_a(tmp);
    }
}

static void ensure_dir_exists_w(const wchar_t *dir_path)
{
    wchar_t tmp[MAX_PATH];
    wchar_t *p;

    if (!dir_path || !*dir_path) return;
    wcsncpy(tmp, dir_path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = L'\0';

    for (p = tmp + 3; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t orig = *p;
            *p = L'\0';
            CreateDirectoryW(tmp, NULL);
            *p = orig;
        }
    }
    CreateDirectoryW(tmp, NULL);
}

static void ensure_parent_dirs_exist_w(const wchar_t *file_path)
{
    wchar_t tmp[MAX_PATH];
    wchar_t *last_sep;

    if (!file_path || !*file_path) return;
    wcsncpy(tmp, file_path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = L'\0';

    last_sep = wcsrchr(tmp, L'\\');
    if (!last_sep) {
        last_sep = wcsrchr(tmp, L'/');
    }
    if (last_sep) {
        *last_sep = L'\0';
        ensure_dir_exists_w(tmp);
    }
}

static bool rewrite_path_a(const char *in_path, char *out_path, size_t out_len, bool is_write)
{
    if (!in_path || !*in_path) return false;

    char clean[MAX_PATH];
    const char *p = in_path;
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
    _snprintf(dev_root, sizeof(dev_root), "%s\\dev", s_game_root_a);
    size_t dev_len = strlen(dev_root);
    if (_strnicmp(clean, dev_root, dev_len) == 0 &&
        (clean[dev_len] == '\0' || clean[dev_len] == '\\' || clean[dev_len] == '/')) {
        return false;
    }

    /* E:\ redirect to <s_game_root_a>\dev\e\... */
    if ((clean[0] == 'e' || clean[0] == 'E') && clean[1] == ':' && (clean[2] == '\\' || clean[2] == '/')) {
        _snprintf(out_path, out_len, "%s\\dev\\e\\%s", s_game_root_a, clean + 3);
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    /* D:\KPM redirect to <s_game_root_a>\... (or dev\ if arcade storage) */
    if (_strnicmp(clean, "d:/kpm", 6) == 0 || _strnicmp(clean, "d:\\kpm", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        if (_strnicmp(rem, "avs_backup", 10) == 0 ||
            _strnicmp(rem, "raw", 3) == 0 ||
            _strnicmp(rem, "nvram", 5) == 0) {
            _snprintf(out_path, out_len, "%s\\dev\\%s", s_game_root_a, rem);
            out_path[out_len - 1] = '\0';
            if (is_write) ensure_parent_dirs_exist_a(out_path);
            return true;
        }
        _snprintf(out_path, out_len, "%s\\%s", s_game_root_a, rem);
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    /* Strip drive letter if present */
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
            _snprintf(out_path, out_len, "%s\\dev\\avs_backup\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\avs_backup", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "raw", 3) == 0 && (s[3] == '\0' || s[3] == '\\' || s[3] == '/')) {
        const char *rem = s + 3;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\RAW\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\RAW", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "nvram", 5) == 0 && (s[5] == '\0' || s[5] == '\\' || s[5] == '/')) {
        const char *rem = s + 5;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\NVRAM\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\NVRAM", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "backup", 6) == 0 && (s[6] == '\0' || s[6] == '\\' || s[6] == '/')) {
        const char *rem = s + 6;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\e\\backup\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\e\\backup", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "log", 3) == 0 && (s[3] == '\0' || s[3] == '\\' || s[3] == '/')) {
        const char *rem = s + 3;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\e\\log\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\e\\log", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "download", 8) == 0 && (s[8] == '\0' || s[8] == '\\' || s[8] == '/')) {
        const char *rem = s + 8;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\e\\download\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\e\\download", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    if (_strnicmp(s, "temp", 4) == 0 && (s[4] == '\0' || s[4] == '\\' || s[4] == '/')) {
        const char *rem = s + 4;
        while (*rem == '\\' || *rem == '/') rem++;
        if (*rem) {
            _snprintf(out_path, out_len, "%s\\dev\\e\\temp\\%s", s_game_root_a, rem);
        } else {
            _snprintf(out_path, out_len, "%s\\dev\\e\\temp", s_game_root_a);
        }
        out_path[out_len - 1] = '\0';
        if (is_write) ensure_parent_dirs_exist_a(out_path);
        return true;
    }

    return false;
}

static bool rewrite_path_w(const wchar_t *in_path, wchar_t *out_path, size_t out_len, bool is_write)
{
    if (!in_path || !*in_path) return false;

    char in_a[MAX_PATH];
    char out_a[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, in_path, -1, in_a, sizeof(in_a), NULL, NULL);

    if (rewrite_path_a(in_a, out_a, sizeof(out_a), is_write)) {
        MultiByteToWideChar(CP_ACP, 0, out_a, -1, out_path, (int) out_len);
        return true;
    }
    return false;
}

/* Function pointers for real kernel32 functions */
static HANDLE (WINAPI *real_CreateFileA)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *real_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static DWORD (WINAPI *real_GetFileAttributesA)(LPCSTR);
static DWORD (WINAPI *real_GetFileAttributesW)(LPCWSTR);
static BOOL (WINAPI *real_GetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL (WINAPI *real_GetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL (WINAPI *real_CreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
static BOOL (WINAPI *real_CreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
static HANDLE (WINAPI *real_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
static HANDLE (WINAPI *real_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static BOOL (WINAPI *real_DeleteFileA)(LPCSTR);
static BOOL (WINAPI *real_DeleteFileW)(LPCWSTR);
static BOOL (WINAPI *real_MoveFileA)(LPCSTR, LPCSTR);
static BOOL (WINAPI *real_MoveFileW)(LPCWSTR, LPCWSTR);
static BOOL (WINAPI *real_MoveFileExA)(LPCSTR, LPCSTR, DWORD);
static BOOL (WINAPI *real_MoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
static BOOL (WINAPI *real_RemoveDirectoryA)(LPCSTR);
static BOOL (WINAPI *real_RemoveDirectoryW)(LPCWSTR);
static BOOL (WINAPI *real_SetCurrentDirectoryA)(LPCSTR);
static BOOL (WINAPI *real_SetCurrentDirectoryW)(LPCWSTR);

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
    bool is_write = (dwDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA)) != 0 ||
                    (dwCreationDisposition != OPEN_EXISTING);
    HANDLE h;

    if (rewrite_path_a(lpFileName, rewrote, MAX_PATH, is_write)) {
        h = real_CreateFileA(
            rewrote, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        log_info("CreateFileA: '%s' -> '%s' (h=%p, err=%lu)",
                 lpFileName, rewrote, h, (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0);
        return h;
    }

    h = real_CreateFileA(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    if (h == INVALID_HANDLE_VALUE) {
        log_warning("CreateFileA unrewritten failed: '%s' (err=%lu)", lpFileName, GetLastError());
    }
    return h;
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
    bool is_write = (dwDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA)) != 0 ||
                    (dwCreationDisposition != OPEN_EXISTING);
    HANDLE h;

    if (rewrite_path_w(lpFileName, rewrote, MAX_PATH, is_write)) {
        h = real_CreateFileW(
            rewrote, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        log_info("CreateFileW: '%ls' -> '%ls' (h=%p, err=%lu)",
                 lpFileName, rewrote, h, (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0);
        return h;
    }

    return real_CreateFileW(
        lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static DWORD WINAPI my_GetFileAttributesA(LPCSTR lpFileName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, MAX_PATH, false)) {
        return real_GetFileAttributesA(rewrote);
    }
    return real_GetFileAttributesA(lpFileName);
}

static DWORD WINAPI my_GetFileAttributesW(LPCWSTR lpFileName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, MAX_PATH, false)) {
        return real_GetFileAttributesW(rewrote);
    }
    return real_GetFileAttributesW(lpFileName);
}

static BOOL WINAPI my_GetFileAttributesExA(
    LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, MAX_PATH, false)) {
        return real_GetFileAttributesExA(rewrote, fInfoLevelId, lpFileInformation);
    }
    return real_GetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
}

static BOOL WINAPI my_GetFileAttributesExW(
    LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, MAX_PATH, false)) {
        return real_GetFileAttributesExW(rewrote, fInfoLevelId, lpFileInformation);
    }
    return real_GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

static BOOL WINAPI my_CreateDirectoryA(
    LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, MAX_PATH, true)) {
        ensure_parent_dirs_exist_a(rewrote);
        return real_CreateDirectoryA(rewrote, lpSecurityAttributes);
    }
    return real_CreateDirectoryA(lpPathName, lpSecurityAttributes);
}

static BOOL WINAPI my_CreateDirectoryW(
    LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, MAX_PATH, true)) {
        ensure_parent_dirs_exist_w(rewrote);
        return real_CreateDirectoryW(rewrote, lpSecurityAttributes);
    }
    return real_CreateDirectoryW(lpPathName, lpSecurityAttributes);
}

static HANDLE WINAPI my_FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    char rewrote[MAX_PATH];
    HANDLE h;
    if (rewrite_path_a(lpFileName, rewrote, MAX_PATH, false)) {
        h = real_FindFirstFileA(rewrote, lpFindFileData);
        log_info("FindFirstFileA: '%s' -> '%s' (h=%p, err=%lu)",
                 lpFileName, rewrote, h, (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0);
        return h;
    }
    h = real_FindFirstFileA(lpFileName, lpFindFileData);
    if (h == INVALID_HANDLE_VALUE) {
        log_warning("FindFirstFileA unrewritten failed: '%s' (err=%lu)", lpFileName, GetLastError());
    }
    return h;
}

static HANDLE WINAPI my_FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    wchar_t rewrote[MAX_PATH];
    HANDLE h;
    if (rewrite_path_w(lpFileName, rewrote, MAX_PATH, false)) {
        h = real_FindFirstFileW(rewrote, lpFindFileData);
        log_info("FindFirstFileW: '%ls' -> '%ls' (h=%p, err=%lu)",
                 lpFileName, rewrote, h, (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0);
        return h;
    }
    h = real_FindFirstFileW(lpFileName, lpFindFileData);
    return h;
}

static BOOL WINAPI my_DeleteFileA(LPCSTR lpFileName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpFileName, rewrote, MAX_PATH, false)) {
        return real_DeleteFileA(rewrote);
    }
    return real_DeleteFileA(lpFileName);
}

static BOOL WINAPI my_DeleteFileW(LPCWSTR lpFileName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpFileName, rewrote, MAX_PATH, false)) {
        return real_DeleteFileW(rewrote);
    }
    return real_DeleteFileW(lpFileName);
}

static BOOL WINAPI my_MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags)
{
    char r_exist[MAX_PATH];
    char r_new[MAX_PATH];
    const char *p_exist = lpExistingFileName;
    const char *p_new = lpNewFileName;

    if (rewrite_path_a(lpExistingFileName, r_exist, MAX_PATH, false)) {
        p_exist = r_exist;
    }
    if (rewrite_path_a(lpNewFileName, r_new, MAX_PATH, true)) {
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

    if (rewrite_path_w(lpExistingFileName, r_exist, MAX_PATH, false)) {
        p_exist = r_exist;
    }
    if (rewrite_path_w(lpNewFileName, r_new, MAX_PATH, true)) {
        p_new = r_new;
    }

    return real_MoveFileExW(p_exist, p_new, dwFlags);
}

static BOOL WINAPI my_MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
    char r_exist[MAX_PATH];
    char r_new[MAX_PATH];
    const char *p_exist = lpExistingFileName;
    const char *p_new = lpNewFileName;

    if (rewrite_path_a(lpExistingFileName, r_exist, MAX_PATH, false)) {
        p_exist = r_exist;
    }
    if (rewrite_path_a(lpNewFileName, r_new, MAX_PATH, true)) {
        ensure_parent_dirs_exist_a(r_new);
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

    if (rewrite_path_w(lpExistingFileName, r_exist, MAX_PATH, false)) {
        p_exist = r_exist;
    }
    if (rewrite_path_w(lpNewFileName, r_new, MAX_PATH, true)) {
        ensure_parent_dirs_exist_w(r_new);
        p_new = r_new;
    }
    return real_MoveFileW(p_exist, p_new);
}

static BOOL WINAPI my_RemoveDirectoryA(LPCSTR lpPathName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, MAX_PATH, false)) {
        return real_RemoveDirectoryA(rewrote);
    }
    return real_RemoveDirectoryA(lpPathName);
}

static BOOL WINAPI my_RemoveDirectoryW(LPCWSTR lpPathName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, MAX_PATH, false)) {
        return real_RemoveDirectoryW(rewrote);
    }
    return real_RemoveDirectoryW(lpPathName);
}

static BOOL WINAPI my_SetCurrentDirectoryA(LPCSTR lpPathName)
{
    char rewrote[MAX_PATH];
    if (rewrite_path_a(lpPathName, rewrote, MAX_PATH, false)) {
        return real_SetCurrentDirectoryA(rewrote);
    }

    char full[MAX_PATH];
    if (GetFullPathNameA(lpPathName, sizeof(full), full, NULL)) {
        size_t root_len = s_game_root_a_len;
        if (_strnicmp(full, s_game_root_a, root_len) != 0 ||
            (full[root_len] != '\0' && full[root_len] != '\\' && full[root_len] != '/')) {
            log_info("Blocked SetCurrentDirectoryA outside root ('%s' -> '%s'), pinning to '%s'",
                     lpPathName, full, s_game_root_a);
            return real_SetCurrentDirectoryA(s_game_root_a);
        }
    }
    return real_SetCurrentDirectoryA(lpPathName);
}

static BOOL WINAPI my_SetCurrentDirectoryW(LPCWSTR lpPathName)
{
    wchar_t rewrote[MAX_PATH];
    if (rewrite_path_w(lpPathName, rewrote, MAX_PATH, false)) {
        return real_SetCurrentDirectoryW(rewrote);
    }

    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(lpPathName, lengthof(full), full, NULL)) {
        size_t root_len = s_game_root_w_len;
        if (_wcsnicmp(full, s_game_root_w, root_len) != 0 ||
            (full[root_len] != L'\0' && full[root_len] != L'\\' && full[root_len] != L'/')) {
            return real_SetCurrentDirectoryW(s_game_root_w);
        }
    }
    return real_SetCurrentDirectoryW(lpPathName);
}

static const struct hook_symbol kpm_kernel32_syms[] = {
    { .name = "CreateFileA",          .patch = my_CreateFileA,          .link = (void **) &real_CreateFileA },
    { .name = "CreateFileW",          .patch = my_CreateFileW,          .link = (void **) &real_CreateFileW },
    { .name = "GetFileAttributesA",   .patch = my_GetFileAttributesA,   .link = (void **) &real_GetFileAttributesA },
    { .name = "GetFileAttributesW",   .patch = my_GetFileAttributesW,   .link = (void **) &real_GetFileAttributesW },
    { .name = "GetFileAttributesExA", .patch = my_GetFileAttributesExA, .link = (void **) &real_GetFileAttributesExA },
    { .name = "GetFileAttributesExW", .patch = my_GetFileAttributesExW, .link = (void **) &real_GetFileAttributesExW },
    { .name = "CreateDirectoryA",     .patch = my_CreateDirectoryA,     .link = (void **) &real_CreateDirectoryA },
    { .name = "CreateDirectoryW",     .patch = my_CreateDirectoryW,     .link = (void **) &real_CreateDirectoryW },
    { .name = "RemoveDirectoryA",     .patch = my_RemoveDirectoryA,     .link = (void **) &real_RemoveDirectoryA },
    { .name = "RemoveDirectoryW",     .patch = my_RemoveDirectoryW,     .link = (void **) &real_RemoveDirectoryW },
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

void kpm_path_hook_init(void)
{
    wchar_t *last_sep_w;
    char *last_sep_a;

    GetModuleFileNameW(NULL, s_game_root_w, MAX_PATH);
    last_sep_w = wcsrchr(s_game_root_w, L'\\');
    if (!last_sep_w) {
        last_sep_w = wcsrchr(s_game_root_w, L'/');
    }
    if (last_sep_w) {
        *last_sep_w = L'\0';
    }
    s_game_root_w_len = wcslen(s_game_root_w);

    GetModuleFileNameA(NULL, s_game_root_a, MAX_PATH);
    last_sep_a = strrchr(s_game_root_a, '\\');
    if (!last_sep_a) {
        last_sep_a = strrchr(s_game_root_a, '/');
    }
    if (last_sep_a) {
        *last_sep_a = '\0';
    }
    s_game_root_a_len = strlen(s_game_root_a);

    log_info("Game root: %s", s_game_root_a);

    /* Pre-create required directory structure for logs, data, and nvram */
    ensure_dir_exists_a(s_game_root_a);
    {
        char sub[MAX_PATH];
        _snprintf(sub, sizeof(sub), "%s\\dev", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\data", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\data\\betslot", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\log", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\backup", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\download", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\e\\temp", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\avs_backup", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\RAW", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\dev\\NVRAM", s_game_root_a);
        ensure_dir_exists_a(sub);
    }

    hook_table_apply(
        NULL,
        "kernel32.dll",
        kpm_kernel32_syms,
        lengthof(kpm_kernel32_syms));

    static const char *const target_modules[] = {
        "KPM_EAM_MOD.DLL",
        "tf_eam_mod.dll",
        "libavs-win32.dll",
        "libavs-win32-ea3.dll",
        "eam3util.dll",
    };

    for (size_t i = 0; i < lengthof(target_modules); i++) {
        HMODULE h = GetModuleHandleA(target_modules[i]);
        if (h) {
            hook_table_apply(
                h, "kernel32.dll", kpm_kernel32_syms, lengthof(kpm_kernel32_syms));
        }
    }

    log_info("Installed comprehensive path redirection hooks for game and dependent modules");

    /* Ensure contents/data/movie exists */
    static char s_movie_dir_a[MAX_PATH];
    _snprintf(s_movie_dir_a, sizeof(s_movie_dir_a), "%s\\data\\movie\\", s_game_root_a);
    ensure_dir_exists_a(s_movie_dir_a);

    /* DirectShow / CMovieCtrl WMV movie path redirection:
     * sub_542600 (0x005426D1) and sub_542C90 (0x00542CC3) push pointer 0x00C3AA58 ("D:\KPM\data\movie\").
     * DirectShow and PathFileExistsA bypass IAT hooks, so we patch the pushed pointers to point directly
     * to s_movie_dir_a, and also patch the string at 0x00C3AA58 to ".\data\movie\" as fallback.
     */
    uint32_t movie_dir_ptr = (uint32_t) s_movie_dir_a;
    DWORD old_protect;

    if (VirtualProtect((void *) 0x005426D2, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy((void *) 0x005426D2, &movie_dir_ptr, sizeof(uint32_t));
        VirtualProtect((void *) 0x005426D2, sizeof(uint32_t), old_protect, &old_protect);
    }

    if (VirtualProtect((void *) 0x00542CC4, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy((void *) 0x00542CC4, &movie_dir_ptr, sizeof(uint32_t));
        VirtualProtect((void *) 0x00542CC4, sizeof(uint32_t), old_protect, &old_protect);
    }

    if (VirtualProtect((void *) 0x00C3AA58, 20, PAGE_EXECUTE_READWRITE, &old_protect)) {
        strcpy((char *) 0x00C3AA58, ".\\data\\movie\\");
        VirtualProtect((void *) 0x00C3AA58, 20, old_protect, &old_protect);
    }
    log_info("Patched DirectShow / CMovieCtrl WMV movie directory to '%s'", s_movie_dir_a);
}

bool kpm_path_rewrite_w(const wchar_t *in_path, wchar_t *out_path, size_t out_len, bool is_write)
{
    return rewrite_path_w(in_path, out_path, out_len, is_write);
}

