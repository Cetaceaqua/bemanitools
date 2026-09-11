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
    const char *rem;
    char combined[MAX_PATH];
    char clean_path[MAX_PATH];
    const char *p;

    if (!in_path || !*in_path) return false;

    p = in_path;
    while (*p == '"' || *p == '\'') {
        p++;
    }
    strncpy(clean_path, p, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';
    size_t len = strlen(clean_path);
    while (len > 0 && (clean_path[len - 1] == '"' || clean_path[len - 1] == '\'')) {
        clean_path[--len] = '\0';
    }

    /* Check d:/kpm or d:\kpm prefix */
    if (_strnicmp(clean_path, "d:/kpm", 6) == 0 || _strnicmp(clean_path, "d:\\kpm", 6) == 0) {
        rem = clean_path + 6;
        while (*rem == '/' || *rem == '\\') {
            rem++;
        }
        if (*rem) {
            _snprintf(combined, sizeof(combined), "%s\\%s", s_game_root_a, rem);
        } else {
            _snprintf(combined, sizeof(combined), "%s", s_game_root_a);
        }
        combined[sizeof(combined) - 1] = '\0';

        /* Resolve relative components (/../, /./) and normalize separators */
        if (!GetFullPathNameA(combined, (DWORD) out_len, out_path, NULL)) {
            strncpy(out_path, combined, out_len);
            out_path[out_len - 1] = '\0';
        }

        if (is_write) {
            ensure_parent_dirs_exist_a(out_path);
        }
        return true;
    }

    /* Check e:/ or e:\ prefix */
    if (_strnicmp(clean_path, "e:/", 3) == 0 || _strnicmp(clean_path, "e:\\", 3) == 0) {
        rem = clean_path + 3;
        while (*rem == '/' || *rem == '\\') {
            rem++;
        }
        if (*rem) {
            _snprintf(combined, sizeof(combined), "%s\\e\\%s", s_game_root_a, rem);
        } else {
            _snprintf(combined, sizeof(combined), "%s\\e", s_game_root_a);
        }
        combined[sizeof(combined) - 1] = '\0';

        if (!GetFullPathNameA(combined, (DWORD) out_len, out_path, NULL)) {
            strncpy(out_path, combined, out_len);
            out_path[out_len - 1] = '\0';
        }

        if (is_write) {
            ensure_parent_dirs_exist_a(out_path);
        }
        return true;
    }

    return false;
}

static bool rewrite_path_w(const wchar_t *in_path, wchar_t *out_path, size_t out_len, bool is_write)
{
    const wchar_t *rem;
    wchar_t combined[MAX_PATH];
    wchar_t clean_path[MAX_PATH];
    const wchar_t *p;

    if (!in_path || !*in_path) return false;

    p = in_path;
    while (*p == L'"' || *p == L'\'') {
        p++;
    }
    wcsncpy(clean_path, p, MAX_PATH - 1);
    clean_path[MAX_PATH - 1] = L'\0';
    size_t len = wcslen(clean_path);
    while (len > 0 && (clean_path[len - 1] == L'"' || clean_path[len - 1] == L'\'')) {
        clean_path[--len] = L'\0';
    }

    /* Check d:/kpm or d:\kpm prefix */
    if (_wcsnicmp(clean_path, L"d:/kpm", 6) == 0 || _wcsnicmp(clean_path, L"d:\\kpm", 6) == 0) {
        rem = clean_path + 6;
        while (*rem == L'/' || *rem == L'\\') {
            rem++;
        }
        if (*rem) {
            _snwprintf(combined, MAX_PATH, L"%s\\%s", s_game_root_w, rem);
        } else {
            _snwprintf(combined, MAX_PATH, L"%s", s_game_root_w);
        }
        combined[MAX_PATH - 1] = L'\0';

        if (!GetFullPathNameW(combined, (DWORD) out_len, out_path, NULL)) {
            wcsncpy(out_path, combined, out_len);
            out_path[out_len - 1] = L'\0';
        }

        if (is_write) {
            ensure_parent_dirs_exist_w(out_path);
        }
        return true;
    }

    /* Check e:/ or e:\ prefix */
    if (_wcsnicmp(clean_path, L"e:/", 3) == 0 || _wcsnicmp(clean_path, L"e:\\", 3) == 0) {
        rem = clean_path + 3;
        while (*rem == L'/' || *rem == L'\\') {
            rem++;
        }
        if (*rem) {
            _snwprintf(combined, MAX_PATH, L"%s\\e\\%s", s_game_root_w, rem);
        } else {
            _snwprintf(combined, MAX_PATH, L"%s\\e", s_game_root_w);
        }
        combined[MAX_PATH - 1] = L'\0';

        if (!GetFullPathNameW(combined, (DWORD) out_len, out_path, NULL)) {
            wcsncpy(out_path, combined, out_len);
            out_path[out_len - 1] = L'\0';
        }

        if (is_write) {
            ensure_parent_dirs_exist_w(out_path);
        }
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
static BOOL (WINAPI *real_MoveFileExA)(LPCSTR, LPCSTR, DWORD);
static BOOL (WINAPI *real_MoveFileExW)(LPCWSTR, LPCWSTR, DWORD);

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

static const struct hook_symbol kpm_kernel32_syms[] = {
    { .name = "CreateFileA", .patch = my_CreateFileA, .link = (void **) &real_CreateFileA },
    { .name = "CreateFileW", .patch = my_CreateFileW, .link = (void **) &real_CreateFileW },
    { .name = "GetFileAttributesA", .patch = my_GetFileAttributesA, .link = (void **) &real_GetFileAttributesA },
    { .name = "GetFileAttributesW", .patch = my_GetFileAttributesW, .link = (void **) &real_GetFileAttributesW },
    { .name = "GetFileAttributesExA", .patch = my_GetFileAttributesExA, .link = (void **) &real_GetFileAttributesExA },
    { .name = "GetFileAttributesExW", .patch = my_GetFileAttributesExW, .link = (void **) &real_GetFileAttributesExW },
    { .name = "CreateDirectoryA", .patch = my_CreateDirectoryA, .link = (void **) &real_CreateDirectoryA },
    { .name = "CreateDirectoryW", .patch = my_CreateDirectoryW, .link = (void **) &real_CreateDirectoryW },
    { .name = "FindFirstFileA", .patch = my_FindFirstFileA, .link = (void **) &real_FindFirstFileA },
    { .name = "FindFirstFileW", .patch = my_FindFirstFileW, .link = (void **) &real_FindFirstFileW },
    { .name = "DeleteFileA", .patch = my_DeleteFileA, .link = (void **) &real_DeleteFileA },
    { .name = "DeleteFileW", .patch = my_DeleteFileW, .link = (void **) &real_DeleteFileW },
    { .name = "MoveFileExA", .patch = my_MoveFileExA, .link = (void **) &real_MoveFileExA },
    { .name = "MoveFileExW", .patch = my_MoveFileExW, .link = (void **) &real_MoveFileExW },
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

    /* Pre-create required directory structure for logs and temp files */
    ensure_dir_exists_a(s_game_root_a);
    {
        char sub[MAX_PATH];
        _snprintf(sub, sizeof(sub), "%s\\e", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\e\\data", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\e\\data\\betslot", s_game_root_a);
        ensure_dir_exists_a(sub);
        _snprintf(sub, sizeof(sub), "%s\\e\\temp", s_game_root_a);
        ensure_dir_exists_a(sub);
    }

    hook_table_apply(
        NULL,
        "kernel32.dll",
        kpm_kernel32_syms,
        lengthof(kpm_kernel32_syms));

    log_info("Installed path redirection hooks for d:/KPM/ and e:/");
}
