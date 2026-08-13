# Run redcli over a folder of DIF files (assumes redcli is already built).
# Usage:
#   .\scripts\test-folder.ps1                  # Release, auto-detect input folder
#   .\scripts\test-folder.ps1 <dir>            # batch a specific folder of .dif
#   .\scripts\test-folder.ps1 <dir> Debug      # same, Debug build
#   .\scripts\test-folder.ps1 -Repeat 5        # change repeat count
#
# The input folder defaults to the first existing of: $env:GOEMS_ROOT,
# $env:GEOMS_ROOT, $env:RLE4K_TEST_DIR, data/, results/, or the repository root.

param(
    [string]$InputDir = '',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release',
    [int]$Repeat = 3
)

$ErrorActionPreference = 'Stop'

# Load shared helpers (repo root + redcli location).
. (Join-Path $PSScriptRoot 'msbuild-common.ps1')
$RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot
Set-Location $RepoRoot

# Locate the previously built executable.
$redcli = Find-RedcliExecutable -RepoRoot $RepoRoot -BuildType $BuildType
# $redcli = "D:\Projects\gitee\ldi\rle4k-cpu\build\Release\redcli.exe"

if (-not $redcli) {
    Write-Error "redcli.exe not found. Run '.\scripts\msbuild.ps1' first."
    exit 1
}

$InputDIr = "D:\Projects\gitee\ldi\gerber_sample_files\geoms"

# Resolve the input folder.
if (-not $InputDir) { $InputDir = $env:GOEMS_ROOT }
if (-not $InputDir) { $InputDir = $env:GEOMS_ROOT }
if (-not $InputDir) { $InputDir = $env:RLE4K_TEST_DIR }
if (-not $InputDir) {
    foreach ($cand in @('data', 'results', '.')) {
        $p = Join-Path $RepoRoot $cand
        if (Test-Path $p) { $InputDir = $p; break }
    }
}
if (-not (Test-Path $InputDir)) {
    Write-Error "Input folder not found: $InputDir (set -InputDir or `$env:RLE4K_TEST_DIR)"
    exit 1
}

# Count .dif files so we can report an empty-folder case clearly.
$difCount = (Get-ChildItem -Path $InputDir -Filter '*.dif' -File -ErrorAction SilentlyContinue).Count
Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host " RLE4K Batch Tests ($BuildType)" -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host "  redcli : $redcli" -ForegroundColor Green
Write-Host "  input  : $InputDir  ($difCount .dif files)" -ForegroundColor Green
Write-Host "  repeat : $Repeat" -ForegroundColor Green
Write-Host ''

if ($difCount -eq 0) {
    Write-Host '  [SKIP] No .dif files found in input folder.' -ForegroundColor DarkGray
    Write-Host '============================================================' -ForegroundColor Cyan
    exit 0
}

# Run redcli in folder mode (it prints a per-file and global summary).
& $redcli run --mode 1 --input $InputDir --repeat $Repeat --verbose file
exit $LASTEXITCODE
