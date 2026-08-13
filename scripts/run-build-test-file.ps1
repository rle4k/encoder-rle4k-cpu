# Build redcli, then run one DIF file.
# Usage (from repository root):
#   .\scripts\run-build-test-file.ps1 path\to\sample.dif
#   .\scripts\run-build-test-file.ps1 path\to\sample.dif Debug

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$DifPath,

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $DifPath)) {
    throw "DIF file not found: $DifPath"
}

& (Join-Path $PSScriptRoot 'msbuild.ps1') -BuildType $BuildType
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

& (Join-Path $PSScriptRoot 'test-single-file.ps1') -InputPath $DifPath -BuildType $BuildType
if ($LASTEXITCODE -ne 0) { throw "Test failed (exit $LASTEXITCODE)" }
