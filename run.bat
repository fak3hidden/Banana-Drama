@echo off
setlocal
REM Runs the injector. With no arguments it lists the running processes and
REM asks which one to inject into.
REM
REM   run.bat
REM   run.bat "Banana Drama.exe"
REM   run.bat "Banana Drama.exe" --wait

pushd "%~dp0"

set "EXE="
if exist "build\x64\Release\BananaDrama.Injector.exe" set "EXE=build\x64\Release\BananaDrama.Injector.exe"
if not defined EXE if exist "build\x64\Debug\BananaDrama.Injector.exe" set "EXE=build\x64\Debug\BananaDrama.Injector.exe"
if not defined EXE if exist "build\Win32\Release\BananaDrama.Injector32.exe" set "EXE=build\Win32\Release\BananaDrama.Injector32.exe"
if not defined EXE if exist "build\Win32\Debug\BananaDrama.Injector32.exe" set "EXE=build\Win32\Debug\BananaDrama.Injector32.exe"

if not defined EXE (
    echo.
    echo   No injector found - build the project first:
    echo.
    echo       compile.bat
    echo.
    pause
    exit /b 1
)

echo.
echo   %EXE%
echo.

"%EXE%" %*

echo.
pause
