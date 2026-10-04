# Builds Release and creates the Workshop artifacts in dist\ (local only: publishing is done by hand):
#   dist\workshop\freecam\                  the mod folder: copy to mods_upload\freecam and upload from the game's Mods menu
#   dist\workshop\freecam_description.txt   text for the Workshop page (Steam BBCode)
#   dist\dk2_freecam-v<version>.sha256.txt  hash of the plugin, for the release notes
#   -RepoUrl    this project's page;  -LoaderUrl  the Native Mod Loader's page (players need it; its releases are linked)
param(
    [string]$RepoUrl = 'https://github.com/gabrweu/doorkickers-freecamera',
    [string]$LoaderUrl = 'https://github.com/gabrweu/doorkickers-modloader'
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$version = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(dk2_freecam VERSION ([\d.]+)').Groups[1].Value
if (-not $version) { throw 'cannot read the version from CMakeLists.txt' }

& (Join-Path $PSScriptRoot 'build.ps1') -NoInstall | Out-Null
$dist = Join-Path $root 'dist'
$workshop = Join-Path $dist 'workshop'
$freecam = Join-Path $workshop 'freecam'
if (Test-Path $freecam) { Remove-Item -LiteralPath $freecam -Recurse -Force }
New-Item -ItemType Directory -Force $freecam | Out-Null
# native\freecam.ini holds the defaults, copied to the player's settings folder on first start
Copy-Item (Join-Path $root 'mod\*') $freecam -Recurse
Copy-Item (Join-Path $root 'build\dk2_freecam.dll') (Join-Path $freecam 'native')
$description = (Get-Content (Join-Path $PSScriptRoot 'workshop_description.txt') -Raw).Replace('@REPO_URL@', $RepoUrl).Replace('@LOADER_URL@', $LoaderUrl)
Set-Content (Join-Path $workshop 'freecam_description.txt') $description -NoNewline

$dll = Join-Path $freecam 'native\dk2_freecam.dll'
$hash = '{0}  {1}' -f (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower(), (Split-Path $dll -Leaf)
Set-Content (Join-Path $dist "dk2_freecam-v$version.sha256.txt") $hash
Write-Host "Workshop: $freecam  (copy to mods_upload\freecam, upload from the Mods menu)"
Write-Host "          $(Join-Path $workshop 'freecam_description.txt')"
Write-Host "  $hash"
if ($RepoUrl -match '<user>' -or $LoaderUrl -match '<user>') {
    Write-Warning 'Pass -RepoUrl and -LoaderUrl: the Workshop description links to them.'
}
