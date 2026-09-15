@echo off
setlocal
REM Grabs the newest version of the project.
REM   - inside a git clone it runs "git pull"
REM   - otherwise it downloads the branch zip and copies the files over
REM
REM Your build output (build\ folder) is kept. Local edits to project files
REM are overwritten, so copy them somewhere first if you care.

pushd "%~dp0"

if exist ".git" (
    echo.
    echo   Updating with git...
    echo.
    git pull --ff-only
    if errorlevel 1 (
        echo.
        echo   git pull failed.
        echo   If you edited tracked files, commit or stash them and try again.
        echo.
        pause
        exit /b 1
    )
    echo.
    echo   Up to date. Run compile.bat to rebuild.
    echo.
    pause
    exit /b 0
)

echo.
echo   No .git folder, downloading the newest zip from GitHub.
echo   Local edits to project files will be overwritten.
echo.

set "ZIP=%TEMP%\BananaDrama-update.zip"
set "UNPACK=%TEMP%\BananaDrama-update"
set "URL1=https://codeload.github.com/fak3hidden/Banana-Drama/zip/refs/heads/arena/01a09c74-banana-drama"
set "URL2=https://github.com/fak3hidden/Banana-Drama/archive/refs/heads/arena/01a09c74-banana-drama.zip"

if exist "%UNPACK%" rmdir /s /q "%UNPACK%"
if exist "%ZIP%" del /q "%ZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; $urls = @('%URL1%','%URL2%'); foreach ($u in $urls) { Write-Host ('Downloading ' + $u); try { Invoke-WebRequest -Uri $u -OutFile '%ZIP%' -UseBasicParsing } catch { Write-Host ('  failed: ' + $_.Exception.Message) }; if (Test-Path '%ZIP%') { break } }"

if not exist "%ZIP%" (
    echo.
    echo   Download failed from both URLs.
    echo.
    echo   Get it by hand instead:
    echo     https://github.com/fak3hidden/Banana-Drama/tree/arena/01a09c74-banana-drama
    echo     Code ^(green button^) ^> Download ZIP
    echo.
    pause
    exit /b 1
)

echo   Extracting...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Path '%ZIP%' -DestinationPath '%UNPACK%' -Force"

set "SRC="
for /d %%d in ("%UNPACK%\*") do set "SRC=%%d"

if not defined SRC (
    echo.
    echo   Could not find the extracted folder.
    echo.
    pause
    exit /b 1
)

echo   Copying files...
xcopy "%SRC%\*" "%~dp0." /E /Y /I /Q

rmdir /s /q "%UNPACK%"
del /q "%ZIP%" 2>nul

echo.
echo   Updated. Run compile.bat to rebuild.
echo.
pause
