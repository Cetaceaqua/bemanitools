@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Packaging Bemanitools KPM Release
echo ========================================================

cd /d "%~dp0"

call build_kpmhook_msvc.bat
if errorlevel 1 (
    echo Error: Build failed!
    exit /b 1
)

set STAGE_DIR=build\package\bemanitools-kpm
set ZIP_FILE=build\bemanitools-kpm.zip

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
mkdir "%STAGE_DIR%"

echo.
echo Staging release files...
copy /Y build\bin\inject.exe "%STAGE_DIR%\" >nul
copy /Y build\bin\kpmhook.dll "%STAGE_DIR%\" >nul
copy /Y build\bin\kpmio.dll "%STAGE_DIR%\" >nul
copy /Y build\bin\geninput.dll "%STAGE_DIR%\" >nul
copy /Y build\bin\eamhook.dll "%STAGE_DIR%\" >nul
copy /Y build\bin\config.exe "%STAGE_DIR%\" >nul
copy /Y dist\kpm\gamestart.bat "%STAGE_DIR%\" >nul
copy /Y dist\kpm\config.bat "%STAGE_DIR%\" >nul
copy /Y dist\kpm\kpmhook.conf "%STAGE_DIR%\" >nul
copy /Y dist\kpm\eamhook.conf "%STAGE_DIR%\" >nul

echo.
echo Creating ZIP archive: %ZIP_FILE%...
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE_DIR%\*' -DestinationPath '%ZIP_FILE%' -Force"
if errorlevel 1 (
    echo Error: Failed to create ZIP archive!
    exit /b 1
)

echo.
echo ========================================================
echo Packaging completed successfully:
echo   %ZIP_FILE%
echo ========================================================
