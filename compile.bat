@echo off
setlocal
REM Builds the dll and the injector without opening Visual Studio.
REM
REM   compile.bat                 Release x64 (default)
REM   compile.bat Debug           Debug x64
REM   compile.bat Release Win32   32-bit build
REM
REM The old dll is deleted first and everything is rebuilt from scratch, so a
REM dll on disk is always from the sources that are on disk right now.

pushd "%~dp0"

set "CONFIG=%~1"
set "PLATFORM=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"

echo.
echo   Banana Drama - rebuilding %CONFIG% ^| %PLATFORM%
echo   Folder: %CD%
echo.

set "OUT=%CD%\build\%PLATFORM%\%CONFIG%"

REM Print when the sources were last changed, so a failed update.bat is obvious:
REM if this is later than the dll below, the download did not replace anything.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%PS%" (
    "%PS%" -NoProfile -ExecutionPolicy Bypass -Command "$s=Get-ChildItem -Path '%CD%\src' -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($s){ Write-Host ('  Newest source : ' + $s.LastWriteTime + '   ' + $s.Name) }"
)

set "MSBUILD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALL=%%i"
)

if defined VSINSTALL (
    if exist "%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined MSBUILD (
    where msbuild >nul 2>nul
    if not errorlevel 1 (
        for /f "delims=" %%i in ('where msbuild') do if not defined MSBUILD set "MSBUILD=%%i"
    )
)

if not defined MSBUILD (
    echo Could not find MSBuild.
    echo.
    echo Install Visual Studio 2022 with the "Desktop development with C++"
    echo workload, or run this from a Developer Command Prompt.
    echo.
    pause
    exit /b 1
)

echo   Using %MSBUILD%
echo.

REM mspdbsrv.exe and a stale pdb are the usual cause of LNK1201 when building
REM with multiple cores, so get rid of both before starting.
taskkill /f /im mspdbsrv.exe >nul 2>&1
if exist "%OUT%\*.pdb" del /q "%OUT%\*.pdb" >nul 2>&1

REM Delete the old output: if the build fails there is no dll to inject by
REM mistake, instead of an old one that crashes the game the same way again.
if exist "%OUT%\BananaDrama.dll" del /q "%OUT%\BananaDrama.dll" >nul 2>&1
if exist "%OUT%\BananaDrama.Injector.exe" del /q "%OUT%\BananaDrama.Injector.exe" >nul 2>&1

"%MSBUILD%" BananaDrama.sln /t:Rebuild /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /v:minimal /nologo

if errorlevel 1 (
    echo.
    echo   BUILD FAILED - scroll up for the first error.
    echo   The old dll was deleted, so there is nothing to inject.
    echo.
    pause
    exit /b 1
)

if not exist "%OUT%\BananaDrama.dll" (
    echo.
    echo   BUILD FAILED - BananaDrama.dll was not produced.
    echo.
    pause
    exit /b 1
)

echo.
echo   BUILD OK
echo.
for %%f in ("%OUT%\BananaDrama.dll") do echo   Built       : %%~tf   %OUT%\BananaDrama.dll
for %%f in ("%OUT%\BananaDrama.Injector.exe") do echo   Injector    : %%~tf   %OUT%\BananaDrama.Injector.exe
echo.
echo   If "Newest source" above is LATER than "Built", update.bat did not
echo   work - the game would still get the old code.
echo.
echo Next: run.bat
echo.
pause
