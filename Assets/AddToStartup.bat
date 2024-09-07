@echo off
REM https://superuser.com/questions/788924/is-it-possible-to-automatically-run-a-batch-file-as-administrator

>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

if %errorlevel% neq 0 (
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0"" ", "", "runas", 1 >> "%temp%\getadmin.vbs"
    "%temp%\getadmin.vbs"
    del "%temp%\getadmin.vbs"
    exit /B
)

cd /d "%~dp0"

set fullPath=%cd%\AltAppSwitcher.exe

if not exist "%fullPath%" (
    msg * "AltAppSwitcher.exe not found."
    exit
)

schtasks /create /sc ONEVENT /ec Application /tn AltAppSwitcher /tr "%fullPath%" /RL HIGHEST /F

reg add "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /t REG_SZ /d "schtasks /run /tn AltAppSwitcher" /f

if %errorlevel% equ 0 (
    msg * "AltAppSwitcher has been added to startup apps. Please re-run this utility if you move the application executable."
    exit
)

pause