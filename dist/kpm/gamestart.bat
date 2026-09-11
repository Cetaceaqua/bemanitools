@echo off

cd /d %~dp0

inject.exe kpmhook.dll KT_SKELETON_ST_DUAL.EXE --config kpmhook.conf %*
