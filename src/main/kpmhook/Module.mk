avsdlls += kpmhook

ldflags_kpmhook := \
    -lws2_32 \
    -luser32 \
    -lgdi32 \
    -ld3d9

libs_kpmhook := \
    hook \
    hooklib \
    cconfig \
    util

src_kpmhook := \
    config-gfx.c \
    config-kpm.c \
    gfx-patch.c \
    path-hook.c \
    touch-hook.c \
    sound-hook.c \
    window-hook.c \
    locale-hook.c \
    d3d9-hook.c \
    dllmain.c
