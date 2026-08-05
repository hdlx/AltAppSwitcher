@echo off

cd /d "%~dp0"

taskkill -f -im "AltAppSwitcher.exe"
pause