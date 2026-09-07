@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building kpmhook1.dll (Win32 / x86 Static CRT)
echo ========================================================

set VCVARS="D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% (
    echo Error: vcvarsall.bat not found at %VCVARS%
    exit /b 1
)

call %VCVARS% x86
if errorlevel 1 exit /b 1

set OUTDIR=build\bin
if not exist %OUTDIR% mkdir %OUTDIR%
set OBJDIR=build\obj\kpmhook1
if not exist %OBJDIR% mkdir %OBJDIR%

set CFLAGS=/nologo /O2 /MT /W3 /I src /I src/main /DPSAPI_VERSION=1 /DBUILD_MODULE=kpmhook1 /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /Fo%OBJDIR%\ /Fd%OBJDIR%\

cl %CFLAGS% /c ^
    src\main\hook\pe.c ^
    src\main\hook\peb.c ^
    src\main\hook\table.c ^
    src\main\hook\com-proxy.c ^
    src\main\util\log.c ^
    src\main\util\mem.c ^
    src\main\util\str.c ^
    src\main\kpmhook1\dllmain.c ^
    src\main\kpmhook1\gfx-patch.c ^
    src\main\kpmhook1\d3d9-hook.c ^
    src\main\kpmhook1\path-hook.c ^
    src\main\kpmhook1\config-kpm.c ^
    src\main\kpmhook1\touch-hook.c ^
    src\main\kpmhook1\sound-hook.c ^
    src\main\kpmhook1\window-hook.c
if errorlevel 1 exit /b 1

set LDFLAGS=/nologo /DLL /DEF:src\main\kpmhook1\kpmhook1.def /OUT:%OUTDIR%\kpmhook1.dll

link %LDFLAGS% ^
    %OBJDIR%\pe.obj ^
    %OBJDIR%\peb.obj ^
    %OBJDIR%\table.obj ^
    %OBJDIR%\com-proxy.obj ^
    %OBJDIR%\log.obj ^
    %OBJDIR%\mem.obj ^
    %OBJDIR%\str.obj ^
    %OBJDIR%\dllmain.obj ^
    %OBJDIR%\gfx-patch.obj ^
    %OBJDIR%\d3d9-hook.obj ^
    %OBJDIR%\path-hook.obj ^
    %OBJDIR%\config-kpm.obj ^
    %OBJDIR%\touch-hook.obj ^
    %OBJDIR%\sound-hook.obj ^
    %OBJDIR%\window-hook.obj ^
    ws2_32.lib user32.lib kernel32.lib d3d9.lib
if errorlevel 1 exit /b 1

echo.
echo ========================================================
echo Successfully built %OUTDIR%\kpmhook1.dll
echo ========================================================
