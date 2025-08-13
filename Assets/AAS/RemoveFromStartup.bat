@echo off

setlocal ENABLEDELAYEDEXPANSION

cd /d "%~dp0"

schtasks /delete /tn AltAppSwitcher /f
if !errorlevel! neq 0 (
    msg * "No startup task to remove. If the task was created as admin, please re-run this utility as admin."
    exit
)

reg delete "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /f

if !errorlevel! neq 0 (
    msg * "No startup task to remove."
    exit
)

msg * "AltAppSwitcher has been removed from startup apps."
