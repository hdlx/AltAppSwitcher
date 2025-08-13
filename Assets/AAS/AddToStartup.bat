@echo off

setlocal ENABLEDELAYEDEXPANSION

cd /d "%~dp0"

set fullPath=%cd%\AltAppSwitcher.exe

if not exist "%fullPath%" (
    msg * "AltAppSwitcher.exe not found."
    exit
)

schtasks /create /sc ONEVENT /ec Application /tn AltAppSwitcher /tr "'%fullPath%'" /RL HIGHEST /F

if !errorlevel! neq 0 (
    schtasks /create /sc ONEVENT /ec Application /tn AltAppSwitcher /tr "'%fullPath%'" /RL LIMITED /F
    if !errorlevel! neq 0 (
        msg * "Task creation failed."
        exit
    )
    set "limited=true"
)

reg add "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /t REG_SZ /d "schtasks /run /tn AltAppSwitcher" /f

if !errorlevel! neq 0 (
    msg * "Adding task to run failed. If a previous task was created as an admin, please re-run this utility as an admin."
    exit
)

if "%limited%" == "true" (
    msg * "Startup task added with limited rights. Re-run this utility as admin for admin rights.
) else (
    msg * "Startup task added. Re-run this utility when moving AltAppSwitcher.exe."
)
