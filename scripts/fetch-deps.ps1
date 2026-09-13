# Downloads the pinned ImGui and MinHook versions into vendor\.
# Needed when you grabbed the repository as a .zip: GitHub does not include
# submodule contents in those, so vendor/imgui and vendor/minhook are empty.
#
#   Right click -> Run with PowerShell
#   or: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$deps = @(
    @{
        name   = 'Dear ImGui'
        url    = 'https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip'
        target = 'vendor/imgui'
        marker = 'imgui.h'
    },
    @{
        name   = 'MinHook'
        url    = 'https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip'
        target = 'vendor/minhook'
        marker = 'include/MinHook.h'
    }
)

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

foreach ($dep in $deps) {
    $target = Join-Path $root $dep.target
    if (Test-Path (Join-Path $target $dep.marker)) {
        Write-Host "$($dep.name) is already in $target - skipping."
        continue
    }

    $zip = Join-Path $env:TEMP "$($dep.name.Replace(' ', '')).zip"
    $temp = Join-Path $env:TEMP "banana-drama-$($dep.name.Replace(' ', ''))"

    Write-Host "Downloading $($dep.name) ..."
    Invoke-WebRequest -Uri $dep.url -OutFile $zip -UseBasicParsing

    if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $temp -Force

    # The archive has one top-level folder (imgui-1.92.9b) - move its contents up.
    $inner = Get-ChildItem -Path $temp -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Get-ChildItem -Path $inner.FullName -Force | ForEach-Object {
        Move-Item -Path $_.FullName -Destination $target -Force
    }

    Remove-Item $temp -Recurse -Force
    Remove-Item $zip -Force
    Write-Host "$($dep.name) ready in $target"
}

Write-Host ''
Write-Host 'Done. Open BananaDrama.sln and build.'
Read-Host 'Press Enter to close'
