<#
.SYNOPSIS
    Package redcli.exe into a versioned zip under releases/.

.PARAMETER Version
    Semantic version, e.g. "1.0.0" (required).

.PARAMETER Tag
    Git tag. Default: encoder-rle4k-cpu-v{Version}.

.PARAMETER Config
    Release (default) or Debug — used to locate the exe.

.PARAMETER OutputDir
    Archive directory. Default: <repo>/releases/.

.PARAMETER PushTag
    Push the Git tag after creating it.

.PARAMETER Force
    Overwrite existing archive / recreate tag.

.PARAMETER DryRun
    Preview only.

.PARAMETER Checksum
    Write SHA256 sidecar next to the zip.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Tag,

    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',

    [string]$OutputDir,

    [switch]$PushTag,
    [switch]$Force,
    [switch]$DryRun,
    [switch]$Checksum
)

$ErrorActionPreference = 'Stop'

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptRoot

if (-not $Tag) { $Tag = "encoder-rle4k-cpu-v$Version" }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot 'releases' }

$ArchiveName = "encoder-rle4k-cpu-v$Version.zip"
$ArchivePath = Join-Path $OutputDir $ArchiveName

$candidates = @(
    (Join-Path $RepoRoot "build\$Config\redcli.exe"),
    (Join-Path $RepoRoot 'build\redcli.exe')
)
$RedcliPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $RedcliPath) {
    Write-Host "[FAIL] redcli.exe not found. Run .\scripts\msbuild.ps1 $Config first." -ForegroundColor Red
    exit 1
}

Write-Host "[FIND] redcli.exe : $RedcliPath" -ForegroundColor Green
Write-Host "  Version : $Version"
Write-Host "  Tag     : $Tag"
Write-Host "  Archive : $ArchivePath"
Write-Host ""

if (-not (Test-Path $OutputDir)) {
    if ($DryRun) {
        Write-Host "[DRY] Would create $OutputDir" -ForegroundColor Cyan
    } else {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }
}

if ((Test-Path -LiteralPath $ArchivePath) -and -not $Force -and -not $DryRun) {
    Write-Host "[WARN] Archive exists: $ArchivePath (use -Force)" -ForegroundColor Yellow
} elseif ($DryRun) {
    Write-Host "[DRY] Would create $ArchivePath with redcli.exe" -ForegroundColor Cyan
} else {
    $StagingDir = Join-Path $env:TEMP ("encoder-rle4k-cpu-release-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
    try {
        Copy-Item -LiteralPath $RedcliPath -Destination (Join-Path $StagingDir 'redcli.exe') -Force
        Compress-Archive -Path (Join-Path $StagingDir '*') -DestinationPath $ArchivePath -Force
        Write-Host "[OK] Archive: $ArchivePath" -ForegroundColor Green
        if ($Checksum) {
            $hash = Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256
            [System.IO.File]::WriteAllText("$ArchivePath.sha256", "$($hash.Hash)  $ArchiveName`n")
            Write-Host "[OK] Checksum: $ArchivePath.sha256" -ForegroundColor Green
        }
    } finally {
        Remove-Item -LiteralPath $StagingDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$existingTag = & git -C $RepoRoot tag -l "$Tag" 2>$null
if ($existingTag) {
    Write-Host "[WARN] Tag exists: $Tag" -ForegroundColor Yellow
    if ($Force -and -not $DryRun) {
        & git -C $RepoRoot tag -d $Tag 2>$null | Out-Null
        & git -C $RepoRoot tag -a $Tag -m "Release $Tag"
        Write-Host "[OK] Recreated tag: $Tag" -ForegroundColor Green
    }
} elseif ($DryRun) {
    Write-Host "[DRY] Would create tag: $Tag" -ForegroundColor Cyan
} else {
    & git -C $RepoRoot tag -a $Tag -m "Release $Tag"
    Write-Host "[OK] Tag: $Tag" -ForegroundColor Green
    if ($PushTag) {
        & git -C $RepoRoot push origin $Tag
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[WARN] Tag push failed; push manually if needed." -ForegroundColor Yellow
        }
    }
}

Write-Host "[DONE] Release $Tag" -ForegroundColor Green
