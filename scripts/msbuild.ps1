
param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

# Load shared helpers (repo root detection, VS/vcpkg/cmake setup).
. (Join-Path $PSScriptRoot 'msbuild-common.ps1')
$RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot
Set-Location $RepoRoot

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host " RLE4K Build: $BuildType | x64" -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan

# [1] Initialize VS dev environment and locate cmake.
Write-Host '[1/4] Initializing build environment...' -ForegroundColor Yellow
$cmake = Initialize-Rle4kBuildEnvironment
if (Test-Rle4kDepsVendorComplete -RepoRoot $RepoRoot) {
    Write-Host '  deps/: vendored packages complete — vcpkg not required for this build' -ForegroundColor Green
} else {
    $null  = Require-VcpkgRoot
    Write-VcpkgStatus
}
# Pick the best generator for the installed VS/cmake pair.
$genInfo = Get-CmakeGeneratorArguments -CmakePath $cmake -BuildType $BuildType

# [2] Clean and create the build directory.
# Also drop VS Open-Folder CMake cache (out/) — it embeds absolute source paths and
# breaks after relocating the repo (CMakeCache CMAKE_HOME_DIRECTORY mismatch).
Write-Host ''
Write-Host '[2/4] Preparing build directory...' -ForegroundColor Yellow
if (Test-Path 'build') {
    Remove-Item -Recurse -Force 'build'
    Write-Host '  Removed existing build/' -ForegroundColor Gray
}
if (Test-Path 'out') {
    Remove-Item -Recurse -Force 'out'
    Write-Host '  Removed existing out/ (stale VS CMake cache)' -ForegroundColor Gray
}
New-Item -ItemType Directory -Force -Path 'build' | Out-Null

# [3] CMake configure + build.
Push-Location build
try {
    Write-Host ''
    Write-Host '[3/4] CMake configure + build...' -ForegroundColor Yellow
    $cmakeArgs = @('..', '-G', $genInfo.Generator) + $genInfo.ExtraArgs + (Get-VcpkgCmakeArguments -BuildType $BuildType)

    $cfgRc = Invoke-WithTempLog -RepoRoot $RepoRoot -LogFileName 'build_log.txt' -Command {
        & $cmake @cmakeArgs
    }
    if ($cfgRc -ne 0) { throw "CMake configure failed (exit $cfgRc)" }

    $buildRc = Invoke-WithTempLog -RepoRoot $RepoRoot -LogFileName 'nmake_output.txt' -Command {
        if ($genInfo.MultiConfig) {
            & $cmake --build . --config $BuildType --parallel
        } else {
            & $cmake --build . --parallel
        }
    }
    if ($buildRc -ne 0) { throw "Build failed (exit $buildRc)" }
} finally {
    Pop-Location
}

# [4] Copy runtime config next to the executable.
Write-Host ''
Write-Host '[4/4] Copying runtime files...' -ForegroundColor Yellow
Copy-RedcliRuntimeFiles -RepoRoot $RepoRoot -BuildType $BuildType

$redcli = Find-RedcliExecutable -RepoRoot $RepoRoot -BuildType $BuildType
if (-not $redcli) { throw 'redcli.exe not found after build' }

$info = Get-Item $redcli
Write-Host "  $($info.FullName)  ($($info.Length) bytes)" -ForegroundColor Green
Write-Host "  logs -> $(Get-Rle4kBuildTempDir -RepoRoot $RepoRoot)\" -ForegroundColor Green

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ' Build SUCCESS' -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan
