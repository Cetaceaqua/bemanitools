avsdlls += kpmhook

ldflags_kpmhook := \
    -lws2_32

libs_kpmhook := \
    hook \
    hooklib \
    cconfig \
    util

src_kpmhook := \
    config-kpm.c \
    gfx-patch.c \
    path-hook.c \
    dllmain.c
