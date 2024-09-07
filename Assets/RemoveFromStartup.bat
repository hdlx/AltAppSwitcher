@echo off
REM https://superuser.com/questions/788924/is-it-possible-to-automatically-run-a-batch-file-as-administrator

>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

if not ERRORLEVEL == 0 (
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0"" ", "", "runas", 1 >> "%temp%\getadmin.vbs"
    "%temp%\getadmin.vbs"
    del "%temp%\getadmin.vbs"
    exit /B
)

cd /d "%~dp0"

schtasks /delete /tn AltAppSwitcher /f
if not ERRORLEVEL == 0 (
    msg * "No startup task to remove."
    exit
)
reg delete "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /f
if not ERRORLEVEL == 0 (
    msg * "No startup task to remove."
    exit
)

msg * "AltAppSwitcher has been removed from startup apps."
