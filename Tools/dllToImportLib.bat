REM 
REM Generate import library file from dll
REM 
REM based on http://stackoverflow.com/questions/9946322/how-to-generate-an-import-library-lib-file-from-a-dll
REM 
REM Please run in VS Command Prompt. needs dumpbin.exe and lib.exe.

@echo off

if "%~1"=="" goto usage
if "%~2"=="" goto usage

dumpbin /exports %~1 > %~n1_exports.txt
IF %ERRORLEVEL% GEQ 1 EXIT /B 2

REM Generate def file.
echo LIBRARY %~n1 > %~n1.def
echo EXPORTS >> %~n1.def
for /f "skip=19 tokens=4" %%A in ( %~n1_exports.txt ) do echo %%A >> %~n1.def

REM Generate lib
lib /def:%~n1.def /out:%~n1.lib /machine:%~2

del %~n1_exports.txt
del %~n1.def
del %~n1.exp

EXIT /B 0

:usage
echo usage: %0 [dll_file] [x64 or x86]