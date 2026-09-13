@echo off
rem Fetches ImGui and MinHook into vendor\ - use this if you downloaded the
rem repository as a .zip, because those do not contain the submodules.
rem If you have git and a real clone, this runs "git submodule update" instead.

pushd "%~dp0.."

where git >nul 2>nul
if %errorlevel%==0 (
    echo Git found, updating submodules...
    git submodule update --init --recursive
    goto :done
)

echo Git not found, downloading the dependencies directly...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-deps.ps1"

:done
popd
