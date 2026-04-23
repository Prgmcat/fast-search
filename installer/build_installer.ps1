<#
.SYNOPSIS
    Build the FastSearch Windows installer (Inno Setup 6).

.DESCRIPTION
    1. Ensure a Release build of all binaries exists.
    2. Locate Inno Setup's ISCC.exe (or install it via winget if missing).
    3. Compile installer\fastsearch.iss -> installer\dist\FastSearch-Setup-<ver>.exe

.PARAMETER SkipBuild
    Skip the cmake build step and just re-run ISCC on existing binaries.

.PARAMETER NoWinget
    Don't try to auto-install Inno Setup even if it's missing; fail instead.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$NoWinget
)

$ErrorActionPreference = 'Stop'

$RepoRoot    = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir    = Join-Path $RepoRoot 'build'
$ReleaseDir  = Join-Path $BuildDir 'Release'
$IssFile     = Join-Path $PSScriptRoot 'fastsearch.iss'
$OutDir      = Join-Path $PSScriptRoot 'dist'
$RequiredArtifacts = @(
    'fastsearch.exe',
    'fastsearch-server.exe',
    'fastsearch-gui.exe',
    'WebView2Loader.dll'
)

function Find-Iscc {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 5\ISCC.exe",
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 5\ISCC.exe',
        'C:\Program Files\Inno Setup 5\ISCC.exe'
    )
    foreach ($p in $candidates) { if ($p -and (Test-Path $p)) { return $p } }
    return $null
}

# Install Inno Setup into %LOCALAPPDATA%\Programs\Inno Setup 6 without
# requiring admin rights — the official installer supports /CURRENTUSER
# since 6.4. This keeps `build_installer.ps1` usable from a normal shell.
function Install-InnoSetupPerUser {
    $url  = 'https://jrsoftware.org/download.php/is.exe'
    $dest = Join-Path $env:TEMP ("innosetup-{0}.exe" -f ([guid]::NewGuid().ToString('N').Substring(0, 8)))
    Write-Host "[+] Downloading Inno Setup 6 installer -> $dest" -ForegroundColor Cyan
    try {
        # -UseBasicParsing for older PS; follow redirect to official mirror.
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    } catch {
        throw "Failed to download Inno Setup installer from $url : $_"
    }
    $target = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6'
    Write-Host "[+] Installing Inno Setup 6 to $target (per-user, no UAC)..." -ForegroundColor Cyan
    # /CURRENTUSER installs for the current user without requiring elevation.
    # /VERYSILENT suppresses UI; /SUPPRESSMSGBOXES avoids error boxes.
    # /DIR explicitly targets the per-user location.
    $args = @('/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOICONS',"/DIR=$target")
    $proc = Start-Process -FilePath $dest -ArgumentList $args -Wait -PassThru
    try { Remove-Item $dest -Force -ErrorAction SilentlyContinue } catch {}
    if ($proc.ExitCode -ne 0) {
        throw "Inno Setup installer exited with code $($proc.ExitCode)."
    }
}

function Ensure-InnoSetup {
    $iscc = Find-Iscc
    if ($iscc) { return $iscc }

    if ($NoWinget) {
        throw "ISCC.exe not found and -NoWinget specified. Install Inno Setup 6 from https://jrsoftware.org/isdl.php"
    }

    # Try per-user install first (works from non-elevated shells).
    try {
        Install-InnoSetupPerUser
    } catch {
        Write-Host "[!] Per-user install failed: $_" -ForegroundColor Yellow
        $winget = Get-Command winget -ErrorAction SilentlyContinue
        if ($winget) {
            Write-Host "[+] Falling back to winget (may require UAC)..." -ForegroundColor Cyan
            & winget install --id JRSoftware.InnoSetup -e --silent `
                --accept-source-agreements --accept-package-agreements | Out-Host
        } else {
            throw "Neither direct download nor winget worked. Install Inno Setup 6 manually from https://jrsoftware.org/isdl.php"
        }
    }

    $iscc = Find-Iscc
    if (-not $iscc) {
        throw "Inno Setup install reported success but ISCC.exe is still not found."
    }
    return $iscc
}

function Invoke-CmakeBuild {
    if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
        Write-Host "[+] Configuring CMake project (first-time)..." -ForegroundColor Cyan
        & cmake -S $RepoRoot -B $BuildDir -A x64
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed." }
    }

    # Stop anything that might be holding an exe open before we rebuild.
    & sc.exe stop FastSearchService 2>$null | Out-Null
    Start-Sleep -Milliseconds 500
    Get-Process -Name 'fastsearch*' -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_ | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    Start-Sleep -Milliseconds 300

    Write-Host "[+] Building Release binaries..." -ForegroundColor Cyan
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw "cmake --build failed." }
}

function Assert-Artifacts {
    $missing = @()
    foreach ($name in $RequiredArtifacts) {
        if (-not (Test-Path (Join-Path $ReleaseDir $name))) { $missing += $name }
    }
    if ($missing.Count -gt 0) {
        throw "Missing build artifacts in $ReleaseDir : $($missing -join ', ')"
    }
}

# ── main ─────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "FastSearch installer build" -ForegroundColor Green
Write-Host "  repo root : $RepoRoot"
Write-Host "  iss file  : $IssFile"
Write-Host "  output    : $OutDir"
Write-Host ""

if (-not $SkipBuild) {
    Invoke-CmakeBuild
} else {
    Write-Host "[i] -SkipBuild specified; using existing Release binaries." -ForegroundColor Yellow
}
Assert-Artifacts

$iscc = Ensure-InnoSetup
Write-Host "[+] Using ISCC: $iscc" -ForegroundColor Cyan

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

Write-Host "[+] Compiling installer..." -ForegroundColor Cyan
# /Qp = quiet progress (show progress lines only), /O = output dir override.
& $iscc "/Qp" "/O$OutDir" $IssFile
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed (exit $LASTEXITCODE)."
}

$installer = Get-ChildItem $OutDir -Filter '*.exe' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $installer) {
    throw "ISCC reported success but no .exe was produced in $OutDir."
}

Write-Host ""
Write-Host ("[OK] Installer built: {0} ({1:N2} MB)" -f `
    $installer.FullName, ($installer.Length / 1MB)) -ForegroundColor Green
