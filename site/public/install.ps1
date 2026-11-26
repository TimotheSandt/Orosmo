<#
.SYNOPSIS
    Orosmo installer for Windows (PowerShell 5.1+).

.DESCRIPTION
    irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1 | iex
    (the same file is served by the site as /install.ps1). With options:
    & ([scriptblock]::Create((irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1))) -Yes

    1. System packages needed to build an Orosmo project: MSYS2 (installed with winget if missing), then in MSYS2 the
       MinGW-w64 GCC 13+ compiler and mingw32-make. The list is shown, then installed after confirmation.
    2. Downloads orosmo-windows-x64.exe of the release, checks it against SHA256SUMS, installs it as
       $OROSMO_HOME\bin\orosmo.exe (default %LOCALAPPDATA%\Orosmo).
    3. Adds $OROSMO_HOME\bin (and MSYS2's mingw64\bin) to the user PATH.
    4. Runs `orosmo install latest` (or -Version): the prebuilt engine goes to $OROSMO_HOME\engines\<version>.

    With `irm | iex` options cannot be passed: the environment variables OROSMO_YES=1, OROSMO_NO_DEPS=1 and
    OROSMO_VERSION=x.y.z do the same. OROSMO_DOWNLOAD_BASE (directory or https:// URL holding the release files) replaces GitHub.

.PARAMETER Yes
    Do not ask (install the system packages, modify PATH).
.PARAMETER NoDeps
    Skip the system packages.
.PARAMETER Version
    Install this release (x.y.z) instead of the latest one.
.PARAMETER NoModifyPath
    Do not modify the user PATH.
.PARAMETER AllowAdmin
    Allow running from an elevated (administrator) shell; refused by default.

.NOTES
    Licence: Apache-2.0 (https://github.com/TimotheSandt/Orosmo).
#>
[CmdletBinding()]
param(
    [switch]$Yes,
    [switch]$NoDeps,
    [string]$Version = "",
    [switch]$NoModifyPath,
    [switch]$AllowAdmin
)

$ErrorActionPreference = "Stop"
$Repo = "TimotheSandt/Orosmo"

# `throw`, not `exit`: with `irm | iex` the script runs in the user's session, which `exit` would close.
function Fail([string]$Message) { throw "orosmo-install: error: $Message" }
function Warn([string]$Message) { Write-Host "orosmo-install: warning: $Message" -ForegroundColor Yellow }

if ($env:OROSMO_YES -eq "1") { $Yes = $true }
if ($env:OROSMO_NO_DEPS -eq "1") { $NoDeps = $true }
if (-not $Version -and $env:OROSMO_VERSION) { $Version = $env:OROSMO_VERSION }
if ($Version) {
    $Version = $Version.TrimStart("v")
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "invalid version '$Version' (expected x.y.z)" }
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -and -not $AllowAdmin) {
    Fail "do not run this installer as administrator: it installs into your user profile (-AllowAdmin to override)"
}
if (-not [Environment]::Is64BitOperatingSystem) { Fail "Orosmo needs 64-bit Windows (x64)" }

$OrosmoHome = if ($env:OROSMO_HOME) { $env:OROSMO_HOME } else { Join-Path $env:LOCALAPPDATA "Orosmo" }
$env:OROSMO_HOME = $OrosmoHome
$BinDir = Join-Path $OrosmoHome "bin"

if ($env:OROSMO_DOWNLOAD_BASE) { $Base = $env:OROSMO_DOWNLOAD_BASE.TrimEnd("/", "\") }
elseif ($Version) { $Base = "https://github.com/$Repo/releases/download/v$Version" }
else { $Base = "https://github.com/$Repo/releases/latest/download" }
# HTTPS only: over plain HTTP the tool and SHA256SUMS would come through the same unauthenticated channel.
if ($Base -match '^http://') { Fail "refusing ${Base}: OROSMO_DOWNLOAD_BASE must be an https:// URL or a local directory" }

function Confirm-Step([string]$Question) {
    if ($Yes) { return $true }
    if (-not [Environment]::UserInteractive) { return $false }
    $answer = Read-Host "$Question [y/N]"
    return $answer -match '^(y|yes|o|oui)$'
}

function Test-Command([string]$Name) { return [bool](Get-Command $Name -ErrorAction SilentlyContinue) }

function Add-UserPath([string]$Dir) {
    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @()
    # @(...): with a single entry, Where-Object returns a string and `$parts + $Dir` would concatenate without ";".
    if ($current) { $parts = @($current.Split(";") | Where-Object { $_ -ne "" }) }
    if ($parts -contains $Dir) { return $false }
    [Environment]::SetEnvironmentVariable("Path", (($parts + $Dir) -join ";"), "User")
    return $true
}

# ── 1. System packages ─────────────────────────────────────────────────────────────
# What an Orosmo project needs from the system on Windows: the MinGW-w64 GCC compiler (C++23: GCC 13+) and GNU make.
# OpenGL (opengl32) comes with Windows, the Vulkan loader (vulkan-1.dll) with the GPU driver; every other third-party
# library is prebuilt and static in the engine package (deps/).
$Msys = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$MsysPackages = @("mingw-w64-x86_64-gcc", "mingw-w64-x86_64-make")
$MingwBin = Join-Path $Msys "mingw64\bin"

if ($NoDeps) {
    Write-Host "System packages: skipped (-NoDeps)."
} else {
    $bash = Join-Path $Msys "usr\bin\bash.exe"
    $steps = @()
    if (-not (Test-Path $bash)) {
        if (Test-Command "winget") {
            $steps += "winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements"
        } else {
            Warn "neither MSYS2 ($Msys) nor winget found: install MSYS2 from https://www.msys2.org, then rerun"
        }
    }
    $steps += "$bash -lc 'pacman -S --needed --noconfirm $($MsysPackages -join ' ')'"
    Write-Host "System packages: MSYS2 + $($MsysPackages -join ', ')"
    Write-Host "Commands:"
    $steps | ForEach-Object { Write-Host "  $_" }
    if (Confirm-Step "Install them now?") {
        if (-not (Test-Path $bash) -and (Test-Command "winget")) {
            winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
            if ($LASTEXITCODE -ne 0) { Fail "winget could not install MSYS2" }
        }
        if (Test-Path $bash) {
            & $bash -lc "pacman -S --needed --noconfirm $($MsysPackages -join ' ')"
            if ($LASTEXITCODE -ne 0) { Fail "pacman could not install $($MsysPackages -join ' ')" }
        }
    } else {
        Warn "system packages not installed: run the commands above yourself (or rerun with -Yes)"
    }
    if (Test-Path $MingwBin) {
        if (-not $NoModifyPath -and (Add-UserPath $MingwBin)) { Write-Host "Added to the user PATH: $MingwBin" }
        $env:Path = "$MingwBin;$env:Path"
    }
    $gpp = Get-Command "g++" -ErrorAction SilentlyContinue
    if ($gpp) {
        $major = [int](((& g++ -dumpversion) -split "\.")[0])
        if ($major -lt 13) { Warn "g++ $major is too old for C++23 (GCC 13+ needed): update MSYS2 (pacman -Syu)" }
        else { Write-Host "C++ compiler: g++ $major" }
    } else {
        Warn "no g++ in PATH: projects will not build until MinGW-w64 GCC 13+ is installed"
    }
}

# ── 2. The orosmo tool ───────────────────────────────────────────────────────────────
$Tool = "orosmo-windows-x64.exe"
$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("orosmo-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $Tmp | Out-Null

# Redirects are followed by hand, HTTPS only: Windows PowerShell 5.1 (.NET Framework HttpWebRequest) would follow an
# HTTPS -> HTTP redirect, and the tool and SHA256SUMS would then travel in clear text.
function Get-HttpsFile([string]$Url, [string]$Dest) {
    for ($hop = 0; $hop -lt 10; $hop++) {
        if ($Url -notmatch '^https://') { throw "refusing $Url (redirect away from HTTPS)" }
        $req = [Net.HttpWebRequest]::Create($Url)
        $req.AllowAutoRedirect = $false
        $req.UserAgent = "orosmo-install"
        $resp = $req.GetResponse()   # 4xx/5xx: WebException, reported by the caller
        try {
            $code = [int]$resp.StatusCode
            if ($code -ge 300 -and $code -lt 400) {
                $location = $resp.Headers["Location"]
                if (-not $location) { throw "HTTP $code without Location for $Url" }
                $Url = (New-Object Uri((New-Object Uri($Url)), $location)).AbsoluteUri
                continue
            }
            if ($code -ne 200) { throw "HTTP $code for $Url" }
            $in = $resp.GetResponseStream()
            $out = [IO.File]::Create($Dest)
            try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
            return
        } finally {
            $resp.Close()
        }
    }
    throw "too many redirects for $Url"
}

function Get-ReleaseFile([string]$Name, [string]$Dest) {
    if ($Base -match '^https://') {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Get-HttpsFile "$Base/$Name" $Dest
    } else {
        Copy-Item -Path (Join-Path ($Base -replace '^file://', '') $Name) -Destination $Dest
    }
}

try {
    Write-Host "Downloading $Tool from $Base ..."
    try { Get-ReleaseFile $Tool (Join-Path $Tmp "orosmo.exe") } catch { Fail "download of $Base/$Tool failed: $_" }
    try { Get-ReleaseFile "SHA256SUMS" (Join-Path $Tmp "SHA256SUMS") } catch { Fail "download of $Base/SHA256SUMS failed: the tool cannot be checked" }
    $expected = $null
    foreach ($line in Get-Content (Join-Path $Tmp "SHA256SUMS")) {
        $f = $line -split '\s+', 2
        if ($f.Count -eq 2 -and $f[1].TrimStart("*") -eq $Tool) { $expected = $f[0].ToLower(); break }
    }
    if (-not $expected) { Fail "SHA256SUMS has no entry for $Tool" }
    $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $Tmp "orosmo.exe")).Hash.ToLower()
    if ($expected -ne $actual) { Fail "checksum mismatch for $Tool (expected $expected, got $actual): corrupted or tampered download" }
    Write-Host "SHA-256 verified."

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    $dest = Join-Path $BinDir "orosmo.exe"
    if (Test-Path $dest) {
        # A running orosmo.exe cannot be overwritten, but it can be renamed.
        Remove-Item -Force -ErrorAction SilentlyContinue "$dest.old"
        Rename-Item -Path $dest -NewName "orosmo.exe.old"
    }
    Move-Item -Path (Join-Path $Tmp "orosmo.exe") -Destination $dest
    Write-Host "Installed $dest"
} finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Tmp
}

# ── 3. PATH ──────────────────────────────────────────────────────────────────────────
if ($NoModifyPath) {
    Write-Host "PATH not modified (-NoModifyPath): add $BinDir to PATH yourself."
} else {
    if (Add-UserPath $BinDir) { Write-Host "Added to the user PATH: $BinDir" } else { Write-Host "PATH: already contains $BinDir" }
    if ($env:OROSMO_HOME -ne (Join-Path $env:LOCALAPPDATA "Orosmo")) {
        [Environment]::SetEnvironmentVariable("OROSMO_HOME", $OrosmoHome, "User")
        Write-Host "OROSMO_HOME set for the user: $OrosmoHome"
    }
}
$env:Path = "$BinDir;$env:Path"

# ── 4. The engine ────────────────────────────────────────────────────────────────────
$target = if ($Version) { $Version } else { "latest" }
Write-Host "Installing the engine ($target) ..."
& (Join-Path $BinDir "orosmo.exe") install $target
if ($LASTEXITCODE -ne 0) { Fail "orosmo install $target failed" }

Write-Host ""
Write-Host "Orosmo is installed. Open a new terminal, then:"
Write-Host "  orosmo init mygame; cd mygame; orosmo run"
