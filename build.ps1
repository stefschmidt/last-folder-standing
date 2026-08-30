<#
.SYNOPSIS
    Builds Last Folder Standing (x64), optionally the installer too.

.EXAMPLE
    .\build.ps1                     # Debug build
    .\build.ps1 -Config Release
    .\build.ps1 -Config Release -Installer
    .\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    # Also compile installer\LastFolderStanding.iss (needs Inno Setup 6).
    [switch]$Installer,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$buildDir = Join-Path $root 'build'

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $buildDir
}

function Find-Tool([string]$Name, [string[]]$Candidates) {
    $cmd = (Get-Command $Name -ErrorAction SilentlyContinue).Source
    if ($cmd) { return $cmd }
    foreach ($c in $Candidates) { if (Test-Path $c) { return $c } }
    return $null
}

$vsRoot = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) { $vsRoot = & $vswhere -latest -property installationPath }

$cmakeCandidates = @()
if ($vsRoot) {
    $cmakeCandidates += (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
}
$cmake = Find-Tool 'cmake' $cmakeCandidates
if (-not $cmake) { throw 'cmake.exe not found. Install CMake or the VS "C++ CMake tools" component.' }

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    # No explicit generator: CMake picks the newest installed Visual Studio.
    & $cmake -S $root -B $buildDir -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
}

& $cmake --build $buildDir --config $Config -- /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

$outDir = Join-Path $buildDir "bin\$Config"
Write-Host ''
Write-Host "Output: $outDir" -ForegroundColor Green
Get-ChildItem $outDir -Include *.exe, *.dll -File -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host ("  {0,-28} {1,10:N0} bytes" -f $_.Name, $_.Length) }

if (-not $Installer) { return }

if ($Config -ne 'Release') {
    throw 'The installer must be built from a Release build (-Config Release).'
}

$iscc = Find-Tool 'ISCC' @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
if (-not $iscc) { throw 'ISCC.exe not found. Install Inno Setup 6.' }

# Keep the installer version in step with the one CMake stamped into the binaries.
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'VERSION\s+(\d+\.\d+\.\d+)' |
            Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw 'Could not read the version from CMakeLists.txt' }

Write-Host ''
Write-Host "Building installer $version" -ForegroundColor Cyan
& $iscc "/DAppVersion=$version" "/DBinDir=$outDir" (Join-Path $root 'installer\LastFolderStanding.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)" }

Get-ChildItem (Join-Path $root 'installer\Output') -Filter *.exe -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host ("  {0}  {1:N0} bytes" -f $_.FullName, $_.Length) -ForegroundColor Green }
