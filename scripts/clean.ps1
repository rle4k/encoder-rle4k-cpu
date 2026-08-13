# Remove local build / IDE caches (does not touch deps/, data/, or source).
# Usage:
#   .\scripts\clean.ps1           # build/, out/
#   .\scripts\clean.ps1 -Deep     # also .vs/, results/

param(
    [switch]$Deep
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'msbuild-common.ps1')
$RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot
Set-Location $RepoRoot

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ' RLE4K Clean' -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan

$targets = @(
    'build',
    'out'
)
if ($Deep) {
    $targets += @('.vs', 'results')
}

$removed = 0
$missing = 0
foreach ($name in $targets) {
    $path = Join-Path $RepoRoot $name
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
        Write-Host "  Removed $name/" -ForegroundColor Gray
        $removed++
    }
    else {
        Write-Host "  Skip $name/ (not present)" -ForegroundColor DarkGray
        $missing++
    }
}

Write-Host ''
if ($removed -eq 0) {
    Write-Host '  Nothing to clean.' -ForegroundColor Green
}
else {
    Write-Host "  Done. Removed $removed path(s)." -ForegroundColor Green
}
if (-not $Deep) {
    Write-Host '  Tip: .\scripts\clean.ps1 -Deep  also removes .vs/ and results/' -ForegroundColor DarkGray
}
Write-Host ''
