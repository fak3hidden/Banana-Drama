@echo off
setlocal
REM Injects the dll that sits next to this script into Banana Drama.
REM
REM   run.bat                     attaches to Banana Drama.exe
REM   run.bat "Banana Drama.exe"
REM   run.bat "Banana Drama.exe" --wait

pushd "%~dp0"

echo.
echo   Folder: %CD%
echo.

set "EXE="
set "DLL="
if exist "build\x64\Release\BananaDrama.Injector.exe" (
    set "EXE=build\x64\Release\BananaDrama.Injector.exe"
    set "DLL=build\x64\Release\BananaDrama.dll"
)
if not defined EXE if exist "build\x64\Debug\BananaDrama.Injector.exe" (
    set "EXE=build\x64\Debug\BananaDrama.Injector.exe"
    set "DLL=build\x64\Debug\BananaDrama.dll"
)
if not defined EXE if exist "build\Win32\Release\BananaDrama.Injector32.exe" (
    set "EXE=build\Win32\Release\BananaDrama.Injector32.exe"
    set "DLL=build\Win32\Release\BananaDrama32.dll"
)
if not defined EXE if exist "build\Win32\Debug\BananaDrama.Injector32.exe" (
    set "EXE=build\Win32\Debug\BananaDrama.Injector32.exe"
    set "DLL=build\Win32\Debug\BananaDrama32.dll"
)

if not defined EXE (
    echo.
    echo   No injector found - build the project first:
    echo.
    echo       compile.bat
    echo.
    pause
    exit /b 1
)

if not exist "%DLL%" (
    echo.
    echo   No dll found at:
    echo       %CD%\%DLL%
    echo.
    echo   Run compile.bat first.
    echo.
    pause
    exit /b 1
)

for %%f in ("%DLL%") do echo   Dll         : %%~tf   %CD%\%DLL%

REM Refuse to inject a dll that is older than the sources: that is exactly how
REM the game ends up running last week's code and crashing the same way.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%PS%" (
    "%PS%" -NoProfile -ExecutionPolicy Bypass -Command "$d=[IO.File]::GetLastWriteTime('%CD%\%DLL%'); $s=Get-ChildItem -Path '%CD%\src' -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($s -and $s.LastWriteTime -gt $d){ Write-Host ''; Write-Host '  THIS DLL IS OLDER THAN THE SOURCES' -ForegroundColor Red; Write-Host ('    sources : ' + $s.LastWriteTime + '   ' + $s.Name) -ForegroundColor Yellow; Write-Host ('    dll     : ' + $d) -ForegroundColor Yellow; Write-Host '  Run compile.bat again before injecting.' -ForegroundColor Yellow; Write-Host ''; exit 2 } else { Write-Host ('  Dll matches the sources (' + $d + ')') -ForegroundColor Green; exit 0 }"
    if errorlevel 2 (
        pause
        exit /b 1
    )
)

echo.
if "%~1"=="" (
    "%EXE%" "Banana Drama.exe" "%CD%\%DLL%"
) else (
    "%EXE%" %* "%CD%\%DLL%"
)

echo.
pause
