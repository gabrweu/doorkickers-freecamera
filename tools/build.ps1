# Builds build\dk2_freecam.dll with MSVC x64, then runs install.ps1. A failed install is only a warning.
#   -Config     the CMake build type (default Release)
#   -NoInstall  build only
param(
    [string]$Config = 'Release',
    [switch]$NoInstall
)

$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 build tools not found' }

$src = Split-Path $PSScriptRoot -Parent
$build = Join-Path $src 'build'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# vcvars calls vswhere by name
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"

cmd /c "`"$vcvars`" >nul && cmake -S `"$src`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build `"$build`""
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

if (-not $NoInstall) {
    try {
        & (Join-Path $PSScriptRoot 'install.ps1') | Out-Null
    } catch {
        Write-Warning "mods_upload\freecam not updated: $_"
    }
}
Join-Path $build 'dk2_freecam.dll'
