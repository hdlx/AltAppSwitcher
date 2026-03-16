@echo off

setlocal ENABLEDELAYEDEXPANSION

cd /d "%~dp0"

schtasks /delete /tn AltAppSwitcher /f >nul 2>&1
if !errorlevel! neq 0 (
    echo "No startup task to remove. If the task was created as admin, please re-run this utility as admin."
    pause
    exit
)

reg delete "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /f >nul 2>&1

if !errorlevel! neq 0 (
    echor "No startup task to remove."
    pause
    exit
)

echo "AltAppSwitcher has been removed from startup apps."
pause