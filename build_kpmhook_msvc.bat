@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building KPM Toolchain: kpmio.dll, kpmhook.dll, config.exe
echo ========================================================

where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    set VCVARS="D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    if exist !VCVARS! (
        call !VCVARS! x86
    ) else (
        for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
            set "VSDIR=%%i"
        )
        if exist "!VSDIR!\VC\Auxiliary\Build\vcvarsall.bat" (
            call "!VSDIR!\VC\Auxiliary\Build\vcvarsall.bat" x86
        ) else (
            echo Error: cl.exe not in PATH and vcvarsall.bat not found.
            exit /b 1
        )
    )
)
if errorlevel 1 exit /b 1

set OUTDIR=build\bin
if not exist %OUTDIR% mkdir %OUTDIR%
set OBJDIR=build\obj\kpmhook
if not exist %OBJDIR% mkdir %OBJDIR%

echo.
echo [1/5] Building geninput.dll...
cl /nologo /O2 /MT /W3 /I src /I src/main /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo%OBJDIR%\ /Fd%OBJDIR%\ /c ^
    src\main\geninput\dev-list.c ^
    src\main\geninput\guid.c ^
    src\main\geninput\hid.c ^
    src\main\geninput\hid-generic.c ^
    src\main\geninput\hid-generic-strings.c ^
    src\main\geninput\hid-meta-in.c ^
    src\main\geninput\hid-meta-out.c ^
    src\main\geninput\hid-mgr.c ^
    src\main\geninput\hid-report-in.c ^
    src\main\geninput\hid-report-out.c ^
    src\main\geninput\hotplug.c ^
    src\main\geninput\input.c ^
    src\main\geninput\io-thread.c ^
    src\main\geninput\kbd.c ^
    src\main\geninput\kbd-data.c ^
    src\main\geninput\mapper.c ^
    src\main\geninput\mapper-s11n.c ^
    src\main\geninput\mouse.c ^
    src\main\geninput\pacdrive.c ^
    src\main\geninput\ri.c
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W3 /I src /I src/main /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo:%OBJDIR%\util-msg-thread.obj /c src\main\util\msg-thread.c
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W3 /I src /I src/main /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo:%OBJDIR%\geninput-msg-thread.obj /c src\main\geninput\msg-thread.c
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W3 /I src /I src/main /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo%OBJDIR%\ /Fd%OBJDIR%\ /c ^
    src\main\util\log.c ^
    src\main\util\mem.c ^
    src\main\util\str.c ^
    src\main\util\thread.c ^
    src\main\util\array.c ^
    src\main\util\fs.c
if errorlevel 1 exit /b 1

link /nologo /DLL /DEF:src\main\geninput\geninput.def /OUT:%OUTDIR%\geninput.dll /IMPLIB:%OBJDIR%\geninput.lib ^
    %OBJDIR%\dev-list.obj ^
    %OBJDIR%\guid.obj ^
    %OBJDIR%\hid.obj ^
    %OBJDIR%\hid-generic.obj ^
    %OBJDIR%\hid-generic-strings.obj ^
    %OBJDIR%\hid-meta-in.obj ^
    %OBJDIR%\hid-meta-out.obj ^
    %OBJDIR%\hid-mgr.obj ^
    %OBJDIR%\hid-report-in.obj ^
    %OBJDIR%\hid-report-out.obj ^
    %OBJDIR%\hotplug.obj ^
    %OBJDIR%\input.obj ^
    %OBJDIR%\io-thread.obj ^
    %OBJDIR%\kbd.obj ^
    %OBJDIR%\kbd-data.obj ^
    %OBJDIR%\mapper.obj ^
    %OBJDIR%\mapper-s11n.obj ^
    %OBJDIR%\mouse.obj ^
    %OBJDIR%\pacdrive.obj ^
    %OBJDIR%\ri.obj ^
    %OBJDIR%\geninput-msg-thread.obj ^
    %OBJDIR%\util-msg-thread.obj ^
    %OBJDIR%\log.obj ^
    %OBJDIR%\mem.obj ^
    %OBJDIR%\str.obj ^
    %OBJDIR%\thread.obj ^
    %OBJDIR%\array.obj ^
    %OBJDIR%\fs.obj ^
    hid.lib setupapi.lib user32.lib kernel32.lib advapi32.lib shell32.lib
if errorlevel 1 exit /b 1

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
set CFLAGS=/nologo /O2 /MT /W3 /I src /I src/main /DWIN32_LEAN_AND_MEAN /DPSAPI_VERSION=1 /DBUILD_MODULE=kpmhook /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /Dstrtok_r=strtok_s /Fo%OBJDIR%\ /Fd%OBJDIR%\

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
    src\main\util\thread.c ^
    src\main\cconfig\cconfig.c ^
    src\main\cconfig\cconfig-hook.c ^
    src\main\cconfig\cconfig-main.c ^
    src\main\cconfig\cconfig-util.c ^
    src\main\cconfig\cmd.c ^
    src\main\cconfig\conf.c ^
    src\main\util\net.c ^
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
    src\main\kpmhook\reader-hook.c ^
    src\main\kpmhook\movie-hook.c ^
    src\main\kpmhook\camera-hook.c ^
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
    %OBJDIR%\thread.obj ^
    %OBJDIR%\cconfig.obj ^
    %OBJDIR%\cconfig-hook.obj ^
    %OBJDIR%\cconfig-main.obj ^
    %OBJDIR%\cconfig-util.obj ^
    %OBJDIR%\cmd.obj ^
    %OBJDIR%\conf.obj ^
    %OBJDIR%\net.obj ^
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
    %OBJDIR%\reader-hook.obj ^
    %OBJDIR%\movie-hook.obj ^
    %OBJDIR%\camera-hook.obj ^
    %OBJDIR%\dllmain.obj ^
    %OUTDIR%\kpmio.lib ^
    %OBJDIR%\eamio.lib ^
    ws2_32.lib user32.lib kernel32.lib gdi32.lib d3d9.lib shell32.lib ole32.lib oleaut32.lib winmm.lib

if errorlevel 1 exit /b 1

echo.
echo [4/5] Building eamhook.dll for eam_if.exe...
set OBJDIR_EAM=build\obj\eamhook
if not exist %OBJDIR_EAM% mkdir %OBJDIR_EAM%
set CFLAGS_EAM=/nologo /O2 /MT /W3 /I src /I src/main /DWIN32_LEAN_AND_MEAN /DPSAPI_VERSION=1 /DBUILD_MODULE=eamhook /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /Fo%OBJDIR_EAM%\ /Fd%OBJDIR_EAM%\

cl %CFLAGS_EAM% /c ^
    src\main\eamhook\config.c ^
    src\main\eamhook\eamuse.c ^
    src\main\eamhook\path.c ^
    src\main\eamhook\dllmain.c
if errorlevel 1 exit /b 1

link /nologo /DLL /DEF:src\main\eamhook\eamhook.def /OUT:%OUTDIR%\eamhook.dll ^
    %OBJDIR%\pe.obj ^
    %OBJDIR%\peb.obj ^
    %OBJDIR%\table.obj ^
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
    %OBJDIR%\net.obj ^
    %OBJDIR_EAM%\config.obj ^
    %OBJDIR_EAM%\eamuse.obj ^
    %OBJDIR_EAM%\path.obj ^
    %OBJDIR_EAM%\dllmain.obj ^
    ws2_32.lib user32.lib kernel32.lib shell32.lib
if errorlevel 1 exit /b 1

echo.
echo [5/5] Building config.exe...
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
echo   %OUTDIR%\geninput.dll
echo   %OUTDIR%\kpmio.dll
echo   %OUTDIR%\kpmhook.dll
echo   %OUTDIR%\eamhook.dll
echo   %OUTDIR%\config.exe
echo ========================================================

set DEST=D:\Arcade_PC\KPM-2012030600\contents
if exist "%DEST%" (
    echo Deploying to %DEST%...
    copy /Y %OUTDIR%\geninput.dll "%DEST%\" >nul
    copy /Y %OUTDIR%\kpmio.dll "%DEST%\" >nul
    copy /Y %OUTDIR%\kpmhook.dll "%DEST%\" >nul
    copy /Y %OUTDIR%\eamhook.dll "%DEST%\" >nul
    copy /Y %OUTDIR%\config.exe "%DEST%\" >nul
    if exist "%DEST%\eamuse\eamhook.dll" del /F "%DEST%\eamuse\eamhook.dll"
    echo Deploy complete.
)
