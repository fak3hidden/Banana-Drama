@echo off
setlocal
REM Injects the dll that sits next to this script into Banana Drama.
REM
REM   run.bat                     attaches to Banana Drama.exe
REM   run.bat "Banana Drama.exe"
REM   run.bat "Banana Drama.exe" --wait
REM
REM Refuses to inject a dll that is older than the sources: that is how the game
REM ends up running old code and crashing the same way over and over.

pushd "%~dp0"

echo.
echo   Folder: %CD%
echo.

if exist "build\x64\Release\BananaDrama.Injector.exe" goto :rel64
if exist "build\x64\Debug\BananaDrama.Injector.exe" goto :dbg64
if exist "build\Win32\Release\BananaDrama.Injector32.exe" goto :rel32
if exist "build\Win32\Debug\BananaDrama.Injector32.exe" goto :dbg32
goto :noinjector

:rel64
set "EXE=build\x64\Release\BananaDrama.Injector.exe"
set "DLL=build\x64\Release\BananaDrama.dll"
goto :found

:dbg64
set "EXE=build\x64\Debug\BananaDrama.Injector.exe"
set "DLL=build\x64\Debug\BananaDrama.dll"
goto :found

:rel32
set "EXE=build\Win32\Release\BananaDrama.Injector32.exe"
set "DLL=build\Win32\Release\BananaDrama32.dll"
goto :found

:dbg32
set "EXE=build\Win32\Debug\BananaDrama.Injector32.exe"
set "DLL=build\Win32\Debug\BananaDrama32.dll"
goto :found

:noinjector
echo   No injector found - build the project first:
echo.
echo       compile.bat
echo.
pause
exit /b 1

:found
if exist "%DLL%" goto :havedll
echo   No dll found at:
echo       %CD%\%DLL%
echo.
echo   Run compile.bat first.
echo.
pause
exit /b 1

:havedll
set "BD_DLL=%CD%\%DLL%"
set "BD_SRC=%CD%\src"

for %%f in ("%DLL%") do echo   Dll         : %%~tf   %BD_DLL%

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" goto :inject
"%PS%" -NoProfile -ExecutionPolicy Bypass -Command "$d=(Get-Item $env:BD_DLL).LastWriteTime; $s=Get-ChildItem -Path $env:BD_SRC -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($s -and $s.LastWriteTime -gt $d){ Write-Host ''; Write-Host '  THIS DLL IS OLDER THAN THE SOURCES' -ForegroundColor Red; Write-Host ('    sources : ' + $s.LastWriteTime + '   ' + $s.Name) -ForegroundColor Yellow; Write-Host ('    dll     : ' + $d) -ForegroundColor Yellow; Write-Host ''; Write-Host '  Run compile.bat again before injecting.' -ForegroundColor Yellow; exit 2 } else { Write-Host ('  Dll matches the sources (' + $d + ')') -ForegroundColor Green; exit 0 }"
if not errorlevel 2 goto :inject
echo.
pause
exit /b 1

:inject
echo.
if "%~1"=="" goto :defaultgame
"%EXE%" %* "%BD_DLL%"
goto :done

:defaultgame
"%EXE%" "Banana Drama.exe" "%BD_DLL%"

:done
echo.
pause
