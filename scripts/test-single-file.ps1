# Run a single DIF file through redcli (assumes redcli is already built).
# Usage:
#   .\scripts\test-single-file.ps1 <file.dif>            # Release (default)
#   .\scripts\test-single-file.ps1 <file.dif> Debug
#   $env:GEOMS_SMOKE_FILE = 'D:\geoms\DIF01.dif'; .\scripts\test-single-file.ps1
#   $env:RLE4K_TEST_DIF   = 'path\to\file.dif'; .\scripts\test-single-file.ps1

param(
    [string]$InputPath = '',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

# Load shared helpers (repo root + redcli location).
. (Join-Path $PSScriptRoot 'msbuild-common.ps1')
$RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot
Set-Location $RepoRoot

# Locate the previously built executable.
$redcli = Find-RedcliExecutable -RepoRoot $RepoRoot -BuildType $BuildType
if (-not $redcli) {
    Write-Error "redcli.exe not found. Run '.\scripts\msbuild.ps1' first."
    exit 1
}

if (-not $InputPath) { $InputPath = $env:GEOMS_SMOKE_FILE }
if (-not $InputPath) { $InputPath = $env:RLE4K_TEST_DIF }

if (-not $InputPath) {
    Write-Error 'Provide a .dif path, or set GEOMS_SMOKE_FILE / RLE4K_TEST_DIF.'
    exit 1
}

if (-not (Test-Path $InputPath)) {
    Write-Error "Input file not found: $InputPath"
    exit 1
}

# CI smoke: RLE4K+LZO2 (0,2) exercises the threaded LZO wrkmem path without
# multi-hour GZIP/BROTLI. Override with RLE4K_SMOKE_FORMATS=0,1,2,3,4,5.
$formats = if ($env:RLE4K_SMOKE_FORMATS) { $env:RLE4K_SMOKE_FORMATS } else { '0,2' }
$encThreads = if ($env:RLE4K_SMOKE_ENCODE_THREADS) { [int]$env:RLE4K_SMOKE_ENCODE_THREADS } else { 4 }

Write-Host ''
Write-Host "  redcli : $redcli" -ForegroundColor Green
Write-Host "  input  : $InputPath" -ForegroundColor Green
Write-Host "  formats: $formats  encode-threads: $encThreads" -ForegroundColor Green
Write-Host ''

# Run redcli in single-file mode.
& $redcli run --mode 0 --input $InputPath --repeat 1 --pw 0.5 --strip-height 512 `
    --formats $formats --encode-threads $encThreads
exit $LASTEXITCODE
