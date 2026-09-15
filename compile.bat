@echo off
setlocal
REM Builds the dll and the injector without opening Visual Studio.
REM
REM   compile.bat                 Release x64 (default)
REM   compile.bat Debug           Debug x64
REM   compile.bat Release Win32   32-bit build

pushd "%~dp0"

set "CONFIG=%~1"
set "PLATFORM=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"

echo.
echo   Banana Drama - building %CONFIG% ^| %PLATFORM%
echo.

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

echo Using %MSBUILD%
echo.

"%MSBUILD%" BananaDrama.sln /t:Build /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /v:minimal /nologo

if errorlevel 1 (
    echo.
    echo   BUILD FAILED - scroll up for the first error.
    echo.
    pause
    exit /b 1
)

echo.
echo   BUILD OK
echo.
echo   build\%PLATFORM%\%CONFIG%\BananaDrama.dll
echo   build\%PLATFORM%\%CONFIG%\BananaDrama.Injector.exe
echo.
echo Next: run.bat  (or double click run.bat and type the game's name)
echo.
pause
