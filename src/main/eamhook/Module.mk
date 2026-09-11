dlls += eamhook

ldflags_eamhook := \
    -lws2_32

libs_eamhook := \
    hook \
    hooklib \
    cconfig \
    util

src_eamhook := \
    config.c \
    eamuse.c \
    path.c \
    dllmain.c
