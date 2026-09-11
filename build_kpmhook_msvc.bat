@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building KPM Toolchain: kpmio.dll, kpmhook.dll, config.exe
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
set OBJDIR=build\obj\kpmhook
if not exist %OBJDIR% mkdir %OBJDIR%

echo.
echo [1/4] Generating import libraries for geninput and eamio...
lib /nologo /machine:x86 /def:src\main\geninput\geninput.def /out:%OBJDIR%\geninput.lib
lib /nologo /machine:x86 /def:src\main\eamio\eamio.def /out:%OBJDIR%\eamio.lib
if errorlevel 1 exit /b 1

echo.
echo [2/4] Building kpmio.dll...
cl /nologo /O2 /MT /W3 /I src /I src/main /Fo:%OBJDIR%\kpmio.obj /c src\main\kpmio\kpmio.c
if errorlevel 1 exit /b 1

link /nologo /DLL /DEF:src\main\kpmio\kpmio.def /OUT:%OUTDIR%\kpmio.dll /IMPLIB:%OUTDIR%\kpmio.lib ^
    %OBJDIR%\kpmio.obj %OBJDIR%\geninput.lib user32.lib kernel32.lib
if errorlevel 1 exit /b 1

echo.
echo [3/4] Building kpmhook.dll...
set CFLAGS=/nologo /O2 /MT /W3 /I src /I src/main /DPSAPI_VERSION=1 /DBUILD_MODULE=kpmhook /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo%OBJDIR%\ /Fd%OBJDIR%\

cl %CFLAGS% /c ^
    src\main\hook\pe.c ^
    src\main\hook\peb.c ^
    src\main\hook\table.c ^
    src\main\hook\com-proxy.c ^
    src\main\util\log.c ^
    src\main\util\mem.c ^
    src\main\util\str.c ^
    src\main\util\hex.c ^
    src\main\util\fs.c ^
    src\main\util\cmdline.c ^
    src\main\cconfig\cconfig.c ^
    src\main\cconfig\cconfig-hook.c ^
    src\main\cconfig\cconfig-main.c ^
    src\main\cconfig\cconfig-util.c ^
    src\main\cconfig\cmd.c ^
    src\main\cconfig\conf.c ^
    src\main\kpmhook\config-gfx.c ^
    src\main\kpmhook\config-io.c ^
    src\main\kpmhook\config-kpm.c ^
    src\main\kpmhook\gfx-patch.c ^
    src\main\kpmhook\d3d9-hook.c ^
    src\main\kpmhook\path-hook.c ^
    src\main\kpmhook\touch-hook.c ^
    src\main\kpmhook\sound-hook.c ^
    src\main\kpmhook\window-hook.c ^
    src\main\kpmhook\locale-hook.c ^
    src\main\kpmhook\io-hook.c ^
    src\main\kpmhook\dllmain.c
if errorlevel 1 exit /b 1

set LDFLAGS=/nologo /DLL /DEF:src\main\kpmhook\kpmhook.def /OUT:%OUTDIR%\kpmhook.dll

link %LDFLAGS% ^
    %OBJDIR%\pe.obj ^
    %OBJDIR%\peb.obj ^
    %OBJDIR%\table.obj ^
    %OBJDIR%\com-proxy.obj ^
    %OBJDIR%\log.obj ^
    %OBJDIR%\mem.obj ^
    %OBJDIR%\str.obj ^
    %OBJDIR%\hex.obj ^
    %OBJDIR%\fs.obj ^
    %OBJDIR%\cmdline.obj ^
    %OBJDIR%\cconfig.obj ^
    %OBJDIR%\cconfig-hook.obj ^
    %OBJDIR%\cconfig-main.obj ^
    %OBJDIR%\cconfig-util.obj ^
    %OBJDIR%\cmd.obj ^
    %OBJDIR%\conf.obj ^
    %OBJDIR%\config-gfx.obj ^
    %OBJDIR%\config-io.obj ^
    %OBJDIR%\config-kpm.obj ^
    %OBJDIR%\gfx-patch.obj ^
    %OBJDIR%\d3d9-hook.obj ^
    %OBJDIR%\path-hook.obj ^
    %OBJDIR%\touch-hook.obj ^
    %OBJDIR%\sound-hook.obj ^
    %OBJDIR%\window-hook.obj ^
    %OBJDIR%\locale-hook.obj ^
    %OBJDIR%\io-hook.obj ^
    %OBJDIR%\dllmain.obj ^
    %OUTDIR%\kpmio.lib ^
    ws2_32.lib user32.lib kernel32.lib gdi32.lib d3d9.lib shell32.lib
if errorlevel 1 exit /b 1

echo.
echo [4/4] Building config.exe...
rc /nologo /i src\main /fo %OBJDIR%\config.res src\main\config\config.rc
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W3 /I src /I src/main /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fo%OBJDIR%\ /c ^
    src\main\config\analogs.c ^
    src\main\config\bind-adv.c ^
    src\main\config\bind-light.c ^
    src\main\config\bind.c ^
    src\main\config\buttons.c ^
    src\main\config\eam.c ^
    src\main\config\gametype.c ^
    src\main\config\lights.c ^
    src\main\config\main.c ^
    src\main\config\schema.c ^
    src\main\config\snap.c ^
    src\main\config\spinner.c ^
    src\main\config\usages.c ^
    src\main\util\array.c ^
    src\main\util\winres.c ^
    src\main\util\thread.c
if errorlevel 1 exit /b 1

link /nologo /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OUT:%OUTDIR%\config.exe ^
    %OBJDIR%\analogs.obj ^
    %OBJDIR%\bind-adv.obj ^
    %OBJDIR%\bind-light.obj ^
    %OBJDIR%\bind.obj ^
    %OBJDIR%\buttons.obj ^
    %OBJDIR%\eam.obj ^
    %OBJDIR%\gametype.obj ^
    %OBJDIR%\lights.obj ^
    %OBJDIR%\main.obj ^
    %OBJDIR%\schema.obj ^
    %OBJDIR%\snap.obj ^
    %OBJDIR%\spinner.obj ^
    %OBJDIR%\usages.obj ^
    %OBJDIR%\array.obj ^
    %OBJDIR%\log.obj ^
    %OBJDIR%\mem.obj ^
    %OBJDIR%\str.obj ^
    %OBJDIR%\winres.obj ^
    %OBJDIR%\thread.obj ^
    %OBJDIR%\config.res ^
    %OBJDIR%\geninput.lib ^
    %OBJDIR%\eamio.lib ^
    comctl32.lib comdlg32.lib gdi32.lib user32.lib kernel32.lib
if errorlevel 1 exit /b 1

echo.
echo ========================================================
echo Successfully built:
echo   %OUTDIR%\kpmio.dll
echo   %OUTDIR%\kpmhook.dll
echo   %OUTDIR%\config.exe
echo ========================================================
