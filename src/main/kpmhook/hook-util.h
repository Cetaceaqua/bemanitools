#ifndef KPMHOOK_HOOK_UTIL_H
#define KPMHOOK_HOOK_UTIL_H

#include <stdint.h>

#if defined(_MSC_VER)

#define KPM_NAKED __declspec(naked)
#define KPM_HELPER_DECL(ret, name, params) static ret name params

#elif defined(__GNUC__)

#define KPM_NAKED __attribute__((naked))
#define KPM_HELPER_DECL(ret, name, params) \
    __attribute__((used, noinline)) static ret name params asm(#name)

#else

#define KPM_NAKED
#define KPM_HELPER_DECL(ret, name, params) static ret name params

#endif

#endif /* KPMHOOK_HOOK_UTIL_H */
