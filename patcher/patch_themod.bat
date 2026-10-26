@echo off
rem Runs patch_themod.ps1 (same folder): makes "THEMOD 1.3.exe" from THUG2.exe.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_themod.ps1"
pause
