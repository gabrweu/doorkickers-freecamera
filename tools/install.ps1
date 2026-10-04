# Developer install: copies the built plugin and the mod files into mods_upload\freecam, the folder the in-game Mods
# menu uploads to the Workshop from, laid out like package.ps1's Workshop folder. Needs the Door Kickers 2 Native
# Mod Loader (dk2ml.dll and dbghelp.dll) in the game folder. The player's settings in the config folder are not touched.
# build.ps1 runs it after every build. It only adds and overwrites files: one removed from mod\ stays in the target
# until deleted by hand, and anything the game writes there survives. A mod.xml there with a different Workshop id
# (ugcId) is kept, so uploads keep updating the published item.
#   -SymTest  the loader repo's symtest.exe, which dry-runs the plugin's init first. Found automatically in
#             ..\doorkickers-modloader\build\ next to this repo. It runs in build\, so its log lands there.
param(
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\DoorKickers2',
    [string]$SymTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$plugin = Join-Path $root 'build\dk2_freecam.dll'
$freecamMod = Join-Path $env:LOCALAPPDATA 'KillHouseGames\DoorKickers2\mods_upload\freecam'

if (-not (Test-Path $plugin)) { throw 'Build first: .\tools\build.ps1' }
if (Get-Process DoorKickers2 -ErrorAction SilentlyContinue) { throw 'Close Door Kickers 2 first (its DLLs are locked).' }

if (-not $SymTest) {
    $candidate = Join-Path $root '..\doorkickers-modloader\build\symtest.exe'
    if (Test-Path $candidate) { $SymTest = $candidate }
}
if ($SymTest) {
    # Exit code 0: init returned 0 and the loader would refuse nothing. 100: a refused call (see the PROBLEM lines).
    Push-Location (Split-Path $plugin)
    try { $out = & $SymTest $GameDir $plugin } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { $out | Write-Host; throw "symtest dry run failed (exit code $LASTEXITCODE)" }
    Write-Host 'symtest: dry run clean'
}

# The mod files (mod.xml, image, gui, native\freecam.ini) come from the repo, the DLL from the build.
New-Item -ItemType Directory -Force (Join-Path $freecamMod 'native') | Out-Null
Get-ChildItem (Join-Path $root 'mod') | Where-Object Name -ne 'mod.xml' | Copy-Item -Destination $freecamMod -Recurse -Force

# Publishing writes the item's Workshop id into the target's mod.xml (ugcId). Without it, or with another one, the
# Mods menu offers to publish a new item instead of updating, so such a mod.xml is never replaced.
function Get-UgcId($path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $m = [regex]::Match((Get-Content -LiteralPath $path -Raw), 'ugcId="(\d+)"')
    if ($m.Success) { $m.Groups[1].Value } else { $null }
}
$repoModXml = Join-Path $root 'mod\mod.xml'
$targetModXml = Join-Path $freecamMod 'mod.xml'
$publishedId = Get-UgcId $targetModXml
if ($publishedId -and $publishedId -ne (Get-UgcId $repoModXml)) {
    Write-Warning "mods_upload\freecam\mod.xml kept: it holds Workshop id $publishedId. Put ugcId=`"$publishedId`" in mod\mod.xml."
} else {
    Copy-Item $repoModXml $targetModXml -Force
}
Copy-Item $plugin (Join-Path $freecamMod 'native') -Force

$missing = 'dk2ml.dll', 'dbghelp.dll' | Where-Object { -not (Test-Path (Join-Path $GameDir $_)) }
if ($missing) { Write-Warning "The mod loader isn't installed in $GameDir (missing: $($missing -join ', '))." }
Write-Host "Installed to $freecamMod. Enable 'Free Camera' in the in-game Mods menu; the loader logs to $GameDir\dk2ml.log."
