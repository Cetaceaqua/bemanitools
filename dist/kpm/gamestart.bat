@echo off

cd /d %~dp0

inject.exe kpmhook.dll KT_SKELETON_ST_DUAL.exe --config kpmhook.conf %*
