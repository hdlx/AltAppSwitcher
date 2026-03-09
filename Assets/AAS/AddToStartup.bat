@echo off

setlocal ENABLEDELAYEDEXPANSION

cd /d "%~dp0"

set fullPath=%cd%\AltAppSwitcher.exe
set workDir=%cd%

if not exist "%fullPath%" (
    msg * "AltAppSwitcher.exe not found."
    exit
)

call :createTask HighestAvailable
if !errorlevel! neq 0 (
    call :createTask LeastPrivilege
    if !errorlevel! neq 0 (
        msg * "Task creation failed. If a previous task was created with admin privileges, please re-run this utility as an admin."
        exit
    )
    set "limited=true"
)

reg add "HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "AltAppSwitcher" /t REG_SZ /d "schtasks /run /tn AltAppSwitcher" /f

if !errorlevel! neq 0 (
    msg * "Adding task to run failed."
    exit
)

if "%limited%" == "true" (
    msg * "Startup task added with limited rights. Re-run this utility as admin for admin rights."
) else (
    msg * "Startup task added. Re-run this utility when moving AltAppSwitcher.exe."
)
exit /b

:createTask
set "curUser=%USERDOMAIN%\%USERNAME%"
set "xmlFile=%TEMP%\AltAppSwitcher_task.xml"
(
echo ^<?xml version="1.0" encoding="UTF-16"?^>
echo ^<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
echo   ^<Triggers^>
echo     ^<LogonTrigger^>
echo       ^<Enabled^>true^</Enabled^>
echo       ^<UserId^>%curUser%^</UserId^>
echo     ^</LogonTrigger^>
echo     ^<SessionStateChangeTrigger^>
echo       ^<Enabled^>true^</Enabled^>
echo       ^<StateChange^>SessionUnlock^</StateChange^>
echo       ^<UserId^>%curUser%^</UserId^>
echo     ^</SessionStateChangeTrigger^>
echo   ^</Triggers^>
echo   ^<Principals^>
echo     ^<Principal id="Author"^>
echo       ^<LogonType^>InteractiveToken^</LogonType^>
echo       ^<RunLevel^>%1^</RunLevel^>
echo     ^</Principal^>
echo   ^</Principals^>
echo   ^<Settings^>
echo     ^<MultipleInstancesPolicy^>IgnoreNew^</MultipleInstancesPolicy^>
echo     ^<DisallowStartIfOnBatteries^>false^</DisallowStartIfOnBatteries^>
echo     ^<StopIfGoingOnBatteries^>false^</StopIfGoingOnBatteries^>
echo     ^<ExecutionTimeLimit^>PT0S^</ExecutionTimeLimit^>
echo     ^<Priority^>7^</Priority^>
echo   ^</Settings^>
echo   ^<Actions^>
echo     ^<Exec^>
echo       ^<Command^>%fullPath%^</Command^>
echo       ^<WorkingDirectory^>%workDir%^</WorkingDirectory^>
echo     ^</Exec^>
echo   ^</Actions^>
echo ^</Task^>
) > "%xmlFile%"

schtasks /create /tn AltAppSwitcher /xml "%xmlFile%" /F

set "result=!errorlevel!"
del "%xmlFile%"
exit /b !result!
