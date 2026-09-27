@echo off
cd /d "%~dp0"

start "" /MIN inject.exe eamhook.dll eamuse\eam_if.exe --config eamhook.conf
timeout /t 1 /nobreak >nul

inject.exe kpmhook.dll KT_SKELETON_ST_DUAL.exe --config kpmhook.conf %*

taskkill /F /IM eam_if.exe >nul 2>&1
