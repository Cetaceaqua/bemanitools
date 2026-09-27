@echo off
cd /d "%~dp0"

echo ========================================================
echo Starting LovePlus MEDAL E-Amusement broker (eam_if)...
echo ========================================================
start "" /MIN inject.exe eamhook.dll eamuse\eam_if.exe --config eamhook.conf

REM Wait 1 second for broker initialization
timeout /t 1 /nobreak >nul

echo ========================================================
echo Starting LovePlus MEDAL game...
echo ========================================================
inject.exe kpmhook.dll KT_SKELETON_ST_DUAL.exe --config kpmhook.conf %*

echo Cleaning up background services...
taskkill /F /IM eam_if.exe >nul 2>&1
