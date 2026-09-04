<#
.SYNOPSIS
    Builds Last Folder Standing, optionally the installer too.

    Everything is x64 except a second copy of the shell extension, which is also
    built as x86 in build-x86\ and copied next to the x64 output. 32-bit
    applications can only load a 32-bit extension DLL, and there are plenty of
    them left.

.EXAMPLE
    .\build.ps1                     # Debug build
    .\build.ps1 -Config Release
    .\build.ps1 -Config Release -Installer
    .\build.ps1 -Config Release -Installer -Sign
    .\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    # Also compile installer\LastFolderStanding.iss (needs Inno Setup 6).
    [switch]$Installer,

    # Authenticode-sign the shipped binaries and, with -Installer, the setup .exe.
    # Needs a code signing certificate in CurrentUser\My; for a hardware token,
    # that token has to be plugged in and unlocked.
    [switch]$Sign,

    # Which certificate to sign with. Optional: without it the only valid code
    # signing certificate in the store is used.
    [string]$CertThumbprint,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$buildDir = Join-Path $root 'build'
$buildDirX86 = Join-Path $root 'build-x86'

if ($Clean) {
    foreach ($dir in @($buildDir, $buildDirX86)) {
        if (Test-Path $dir) {
            Write-Host "Removing $dir" -ForegroundColor DarkGray
            Remove-Item -Recurse -Force $dir
        }
    }
}

function Find-Tool([string]$Name, [string[]]$Candidates) {
    $cmd = (Get-Command $Name -ErrorAction SilentlyContinue).Source
    if ($cmd) { return $cmd }
    foreach ($c in $Candidates) { if (Test-Path $c) { return $c } }
    return $null
}

# 1.3.6.1.5.5.7.3.3 is the code signing EKU. Matching on the OID instead of the
# friendly name keeps this working on a non-English Windows.
function Find-SigningCert([string]$Thumbprint) {
    if ($Thumbprint) {
        $cert = Get-Item "Cert:\CurrentUser\My\$Thumbprint" -ErrorAction SilentlyContinue
        if (-not $cert) { throw "No certificate $Thumbprint in CurrentUser\My." }
        return $cert
    }

    $now = Get-Date
    $found = @(Get-ChildItem Cert:\CurrentUser\My | Where-Object {
        $_.NotBefore -le $now -and $_.NotAfter -gt $now -and
        $_.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3'
    })

    if ($found.Count -eq 0) {
        throw 'No valid code signing certificate in CurrentUser\My. Pass -CertThumbprint.'
    }
    if ($found.Count -gt 1) {
        throw ('Several code signing certificates found, pass -CertThumbprint with one of: ' +
               (($found | ForEach-Object { $_.Thumbprint }) -join ', '))
    }
    return $found[0]
}

# Timestamped, so signatures stay valid after the certificate expires.
function Invoke-SignTool([string]$Tool, [string]$Thumbprint, [string[]]$Files) {
    & $Tool sign /sha1 $Thumbprint /fd SHA256 /tr 'http://timestamp.sectigo.com' /td SHA256 $Files
    if ($LASTEXITCODE -ne 0) { throw "Signing failed ($LASTEXITCODE)" }
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

# One build tree per architecture: the Visual Studio generator bakes the target
# platform into the cache, so a single tree cannot produce both.
function Invoke-Build([string]$Dir, [string]$Platform) {
    if (-not (Test-Path (Join-Path $Dir 'CMakeCache.txt'))) {
        # No explicit generator: CMake picks the newest installed Visual Studio.
        & $cmake -S $root -B $Dir -A $Platform
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $Platform ($LASTEXITCODE)" }
    }

    & $cmake --build $Dir --config $Config -- /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Platform ($LASTEXITCODE)" }
}

Invoke-Build $buildDir 'x64'
Write-Host ''
Write-Host 'Building the 32-bit shell extension' -ForegroundColor Cyan
Invoke-Build $buildDirX86 'Win32'

$outDir = Join-Path $buildDir "bin\$Config"
$outDirX86 = Join-Path $buildDirX86 "bin\$Config"

# The installer picks everything up from one directory, so the 32-bit DLL moves
# in next to the x64 one. Different file names, hence no collision.
$dll32 = Join-Path $outDirX86 'LFS.ShellExtension32.dll'
if (-not (Test-Path $dll32)) { throw "Missing 32-bit extension: $dll32" }
Copy-Item $dll32 -Destination $outDir -Force

# Everything that ends up on a user's machine. The probes are dev-only tools and
# are never shipped, so they stay out of it.
$shipped = @(
    'LFS.Monitor.exe',
    'LFS.Settings.exe',
    'LFS.ShellExtension.dll',
    'LFS.ShellExtension32.dll'
) | ForEach-Object { Join-Path $outDir $_ }

# Signing runs before Inno Setup compiles, otherwise the installer would pack
# unsigned files inside a signed setup.exe.
if ($Sign) {
    $signtoolCandidates = @(
        Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $_.FullName }
    )
    $signtool = Find-Tool 'signtool' $signtoolCandidates
    if (-not $signtool) { throw 'signtool.exe not found. Install the Windows SDK.' }

    $cert = Find-SigningCert $CertThumbprint
    Write-Host ''
    Write-Host "Signing as $($cert.Subject)" -ForegroundColor Cyan
    Invoke-SignTool $signtool $cert.Thumbprint $shipped
}

Write-Host ''
Write-Host "Output: $outDir" -ForegroundColor Green
Get-ChildItem "$outDir\*" -Include *.exe, *.dll -File -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host ("  {0,-28} {1,10:N0} bytes" -f $_.Name, $_.Length) }
Write-Host "32-bit probe: $outDirX86\shellext_probe.exe" -ForegroundColor DarkGray

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

$setup = Join-Path $root "installer\Output\LastFolderStanding-$version-setup.exe"
if ($Sign) {
    if (-not (Test-Path $setup)) { throw "Setup not found where expected: $setup" }
    Invoke-SignTool $signtool $cert.Thumbprint @($setup)
}

Get-ChildItem (Join-Path $root 'installer\Output') -Filter *.exe -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host ("  {0}  {1:N0} bytes" -f $_.FullName, $_.Length) -ForegroundColor Green }
