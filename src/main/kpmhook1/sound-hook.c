#define LOG_MODULE "kpm-sound"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kpmhook1/sound-hook.h"
#include "kpmhook1/path-hook.h"
#include "util/defs.h"
#include "util/log.h"

static void log_sub_5E3F50_null(void)
{
    log_warning("sub_5E3F50 called with invalid CWavDataList (ESI < 0x1000), skipping");
}

static void log_sub_5E2D70_null(void)
{
    log_warning("sub_5E2D70 called with invalid CWavDataList (a1 < 0x1000), returning NULL group");
}

/*
 * sub_5E2D70 hook:
 * Original signature: int __stdcall sub_5E2D70(int a1, int a2)
 * a1 is CWavDataList*.
 * If a1 < 0x1000, returns 0 immediately (ret 8).
 * Original prologue at 0x005E2D70:
 *   55             push ebp
 *   8B EC          mov ebp, esp
 *   83 E4 F8       and esp, -8
 *   (6 bytes) -> continues at 0x005E2D76
 */
static const uintptr_t SUB_5E2D70_ADDR = 0x005E2D70;
static const uintptr_t SUB_5E2D70_CONT = 0x005E2D76;

static __declspec(naked) void sub_5E2D70_hook(void)
{
    __asm {
        mov eax, [esp + 4]
        cmp eax, 0x1000
        jb null_a1
        push ebp
        mov ebp, esp
        and esp, 0xFFFFFFF8
        mov eax, SUB_5E2D70_CONT
        jmp eax
null_a1:
        pushad
        call log_sub_5E2D70_null
        popad
        xor eax, eax
        ret 8
    }
}

/*
 * sub_5E3F50 hook:
 * Original prologue at 0x005E3F50:
 *   55             push ebp
 *   8B EC          mov ebp, esp
 *   83 E4 F8       and esp, -8
 *   (6 bytes) -> continues at 0x005E3F56
 * ESI is CWavDataList*. If ESI < 0x1000, returns early (ret 4).
 */
static const uintptr_t SUB_5E3F50_ADDR = 0x005E3F50;
static const uintptr_t SUB_5E3F50_CONT = 0x005E3F56;

static __declspec(naked) void sub_5E3F50_hook(void)
{
    __asm {
        cmp esi, 0x1000
        jb null_ret
        push ebp
        mov ebp, esp
        and esp, 0xFFFFFFF8
        mov eax, SUB_5E3F50_CONT
        jmp eax
null_ret:
        pushad
        call log_sub_5E3F50_null
        popad
        ret 4
    }
}

/*
 * sub_5EAE10 hook:
 * Original signature: int __usercall sub_5EAE10@<eax>(const char *a1@<eax>, int a2@<edi>)
 * CSndScptList::Load(filename)
 * Strips any surrounding quotes, rewrites d:/kpm paths to local directory.
 */
static const uintptr_t SUB_5EAE10_ADDR = 0x005EAE10;
static const uintptr_t SUB_5EAE10_CONT = 0x005EAE17;

static char s_s3b_rewritten[MAX_PATH];

static const char *rewrite_s3b_path(const char *in_path)
{
    if (!in_path || !*in_path) return in_path;

    /* Strip leading quotes */
    const char *p = in_path;
    while (*p == '"' || *p == '\'') p++;

    char clean[MAX_PATH];
    strncpy(clean, p, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';

    /* Strip trailing quotes */
    size_t len = strlen(clean);
    while (len > 0 && (clean[len - 1] == '"' || clean[len - 1] == '\'')) {
        clean[--len] = '\0';
    }

    /* Get module directory */
    char root[MAX_PATH];
    GetModuleFileNameA(NULL, root, MAX_PATH);
    char *last = strrchr(root, '\\');
    if (!last) last = strrchr(root, '/');
    if (last) *last = '\0';

    if (_strnicmp(clean, "d:/kpm", 6) == 0 || _strnicmp(clean, "d:\\kpm", 6) == 0) {
        const char *rem = clean + 6;
        while (*rem == '/' || *rem == '\\') rem++;
        char combined[MAX_PATH];
        _snprintf(combined, sizeof(combined), "%s\\%s", root, rem);
        combined[sizeof(combined) - 1] = '\0';
        if (!GetFullPathNameA(combined, MAX_PATH, s_s3b_rewritten, NULL)) {
            strncpy(s_s3b_rewritten, combined, MAX_PATH);
        }
        log_info("sub_5EAE10 (S3B): '%s' -> '%s'", in_path, s_s3b_rewritten);
        return s_s3b_rewritten;
    }

    strncpy(s_s3b_rewritten, clean, MAX_PATH);
    s_s3b_rewritten[MAX_PATH - 1] = '\0';
    return s_s3b_rewritten;
}

static __declspec(naked) void sub_5EAE10_hook(void)
{
    __asm {
        test edi, edi
        jz null_edi
        pushad
        push eax
        call rewrite_s3b_path
        add esp, 4
        mov [esp + 28], eax /* replace saved EAX in pushad frame with rewritten pointer */
        popad
        /* execute original displaced instructions:
           push esi (1)
           push offset aRb (5)
           push eax (1)
           total = 7 bytes. Next instruction is at 0x005EAE17: mov [edi+4], 0
         */
        push esi
        push 0x00BBC48C /* offset "rb" */
        push eax
        mov eax, SUB_5EAE10_CONT
        jmp eax
null_edi:
        xor eax, eax
        ret
    }
}

void kpm_sound_hook_init(void)
{
    DWORD old_prot;

    /* Hook sub_5E2D70 (6 bytes: 5 byte JMP + 1 NOP) */
    VirtualProtect((void *) SUB_5E2D70_ADDR, 6, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t jmp_buf0[6];
    jmp_buf0[0] = 0xE9; /* JMP rel32 */
    *((int32_t *) &jmp_buf0[1]) = (int32_t) ((uintptr_t) sub_5E2D70_hook - (SUB_5E2D70_ADDR + 5));
    jmp_buf0[5] = 0x90; /* NOP */
    memcpy((void *) SUB_5E2D70_ADDR, jmp_buf0, 6);
    VirtualProtect((void *) SUB_5E2D70_ADDR, 6, old_prot, &old_prot);

    /* Hook sub_5E3F50 (6 bytes: 5 byte JMP + 1 NOP) */
    VirtualProtect((void *) SUB_5E3F50_ADDR, 6, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t jmp_buf1[6];
    jmp_buf1[0] = 0xE9; /* JMP rel32 */
    *((int32_t *) &jmp_buf1[1]) = (int32_t) ((uintptr_t) sub_5E3F50_hook - (SUB_5E3F50_ADDR + 5));
    jmp_buf1[5] = 0x90; /* NOP */
    memcpy((void *) SUB_5E3F50_ADDR, jmp_buf1, 6);
    VirtualProtect((void *) SUB_5E3F50_ADDR, 6, old_prot, &old_prot);

    /* Hook sub_5EAE10 (7 bytes: 5 byte JMP + 2 NOPs) */
    VirtualProtect((void *) SUB_5EAE10_ADDR, 7, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t jmp_buf2[7];
    jmp_buf2[0] = 0xE9; /* JMP rel32 */
    *((int32_t *) &jmp_buf2[1]) = (int32_t) ((uintptr_t) sub_5EAE10_hook - (SUB_5EAE10_ADDR + 5));
    jmp_buf2[5] = 0x90; /* NOP */
    jmp_buf2[6] = 0x90; /* NOP */
    memcpy((void *) SUB_5EAE10_ADDR, jmp_buf2, 7);
    VirtualProtect((void *) SUB_5EAE10_ADDR, 7, old_prot, &old_prot);

    /* Patch 0x005EAE74 (mov byte ptr [eax+95h], 1 -> 0) so fatal error flag is never set */
    uintptr_t fatal_flag_addr = 0x005EAE74;
    VirtualProtect((void *) fatal_flag_addr, 1, PAGE_EXECUTE_READWRITE, &old_prot);
    *((uint8_t *) fatal_flag_addr) = 0x00;
    VirtualProtect((void *) fatal_flag_addr, 1, old_prot, &old_prot);

    log_info("Installed sound library crash prevention hooks (sub_5E2D70, sub_5E3F50, sub_5EAE10, assert flag)");
}
