avsdlls += kpmhook1

ldflags_kpmhook1 := \
    -lws2_32

libs_kpmhook1 := \
    hook \
    hooklib \
    cconfig \
    util

src_kpmhook1 := \
    config-kpm.c \
    gfx-patch.c \
    path-hook.c \
    dllmain.c
