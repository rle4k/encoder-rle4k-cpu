# Build, run unit tests, and enforce library line coverage >= 90%.
# Usage:
#   .\scripts\run-unit-tests.ps1
#   .\scripts\run-unit-tests.ps1 -BuildType Debug -SkipCoverage
#   .\scripts\run-unit-tests.ps1 -MinCoverage 90

param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release',
    [double]$MinCoverage = 90.0,
    [switch]$SkipCoverage,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'msbuild-common.ps1')
$RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot
Set-Location $RepoRoot

function Find-RedcliTestsExecutable {
    param([string]$Root, [string]$Config)
    $candidates = @(
        (Join-Path $Root "build\tests\$Config\redcli_tests.exe"),
        (Join-Path $Root "build\tests\redcli_tests.exe"),
        (Join-Path $Root "build\$Config\redcli_tests.exe"),
        (Join-Path $Root "build\redcli_tests.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Find-CoverageConsole {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -property installationPath 2>$null
        if ($install) {
            $cand = Join-Path $install 'Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe'
            if (Test-Path $cand) { return $cand }
        }
    }
    $fallback = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe'
    if (Test-Path $fallback) { return $fallback }
    return $null
}

function Get-CoberturaLibraryLinePct {
    param(
        [string]$XmlPath,
        [string]$SourceRoot
    )
    [xml]$doc = Get-Content -LiteralPath $XmlPath -Raw
    $srcNorm = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/').ToLowerInvariant()

    $linesValid = 0
    $linesCovered = 0
    $classes = @()
    if ($doc.coverage.packages.package) {
        foreach ($pkg in @($doc.coverage.packages.package)) {
            if ($pkg.classes.class) { $classes += @($pkg.classes.class) }
        }
    }
    if ($classes.Count -eq 0 -and $doc.SelectNodes) {
        $classes = @($doc.SelectNodes('//class'))
    }

    foreach ($cls in $classes) {
        $fname = [string]$cls.filename
        if ([string]::IsNullOrWhiteSpace($fname)) { continue }
        $full = $fname
        try { $full = [System.IO.Path]::GetFullPath($full) } catch { continue }
        $fullLower = $full.ToLowerInvariant()
        if (-not $fullLower.StartsWith($srcNorm)) { continue }
        if ($fullLower -notmatch '\.cpp$') { continue }   # library .cpp only
        if ($fullLower.EndsWith('\main.cpp') -or $fullLower.EndsWith('/main.cpp')) { continue }

        $lineNodes = @()
        if ($cls.lines.line) { $lineNodes = @($cls.lines.line) }
        elseif ($cls.SelectNodes) { $lineNodes = @($cls.SelectNodes('.//line')) }
        foreach ($line in $lineNodes) {
            $linesValid++
            $hits = 0
            [void][int]::TryParse([string]$line.hits, [ref]$hits)
            if ($hits -gt 0) { $linesCovered++ }
        }
    }

    if ($linesValid -eq 0) {
        throw "No library .cpp lines found in $XmlPath (source filter: $SourceRoot)"
    }
    return (100.0 * $linesCovered / $linesValid)
}

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host " RLE4K Unit Tests | $BuildType | gate >= $MinCoverage%" -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan

if (-not $SkipBuild) {
    Write-Host '[1/3] Configure + build (with tests)...' -ForegroundColor Yellow
    $cmake = Initialize-Rle4kBuildEnvironment
    if (Test-Rle4kDepsVendorComplete -RepoRoot $RepoRoot) {
        Write-Host '  deps/: vendored packages complete — vcpkg not required for this build' -ForegroundColor Green
    } else {
        $null = Require-VcpkgRoot
        Write-VcpkgStatus
    }
    $genInfo = Get-CmakeGeneratorArguments -CmakePath $cmake -BuildType $BuildType
    $buildDir = Join-Path $RepoRoot 'build'
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    }

    Push-Location $buildDir
    try {
        $cmakeArgs = @('..', '-G', $genInfo.Generator) + $genInfo.ExtraArgs +
            (Get-VcpkgCmakeArguments -BuildType $BuildType) +
            @('-DRLE4K_BUILD_TESTS=ON', '-DRLE4K_COVERAGE:BOOL=ON')
        Write-Host ("  cmake " + ($cmakeArgs -join ' ')) -ForegroundColor Gray
        $cfgRc = Invoke-WithTempLog -RepoRoot $RepoRoot -LogFileName 'test_build_log.txt' -Command {
            & $cmake @cmakeArgs
        }
        if ($cfgRc -ne 0) { throw "CMake configure failed (exit $cfgRc)" }

        $covCache = Select-String -Path (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^RLE4K_COVERAGE:BOOL=(.*)$'
        if (-not $covCache -or $covCache.Matches[0].Groups[1].Value -ne 'ON') {
            throw "RLE4K_COVERAGE did not stick in CMakeCache (expected ON). Reconfigure failed?"
        }
        Write-Host '  RLE4K_COVERAGE=ON confirmed in CMakeCache' -ForegroundColor Green

        $buildRc = Invoke-WithTempLog -RepoRoot $RepoRoot -LogFileName 'test_nmake_output.txt' -Command {
            if ($genInfo.MultiConfig) {
                & $cmake --build . --config $BuildType --parallel --target redcli_tests
            } else {
                & $cmake --build . --parallel --target redcli_tests
            }
        }
        if ($buildRc -ne 0) { throw "Build redcli_tests failed (exit $buildRc)" }
    } finally {
        Pop-Location
    }
}

$testExe = Find-RedcliTestsExecutable -Root $RepoRoot -Config $BuildType
if (-not $testExe) { throw 'redcli_tests.exe not found. Build first (omit -SkipBuild).' }
Write-Host "[2/3] Running $testExe" -ForegroundColor Yellow

$outDir = Get-Rle4kBuildOutDir -RepoRoot $RepoRoot
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$gtestLog = Join-Path $outDir 'gtest_unit.xml'
$gtestOut = Join-Path $outDir 'gtest_stdout.txt'
$gtestErr = Join-Path $outDir 'gtest_stderr.txt'
$proc = Start-Process -FilePath $testExe `
    -ArgumentList @("--gtest_output=xml:$gtestLog") `
    -WorkingDirectory (Split-Path $testExe -Parent) `
    -Wait -PassThru -NoNewWindow `
    -RedirectStandardOutput $gtestOut `
    -RedirectStandardError $gtestErr
if (Test-Path $gtestOut) { Get-Content -LiteralPath $gtestOut | Write-Host }
if (Test-Path $gtestErr) { Get-Content -LiteralPath $gtestErr | ForEach-Object { Write-Host $_ -ForegroundColor DarkGray } }
if ($proc.ExitCode -ne 0) {
    throw "Unit tests failed (exit $($proc.ExitCode)). See $gtestLog"
}
Write-Host '  Unit tests PASSED' -ForegroundColor Green

if ($SkipCoverage) {
    Write-Host '[3/3] Coverage skipped (-SkipCoverage)' -ForegroundColor Yellow
    exit 0
}

Write-Host '[3/3] Measuring library line coverage (exclude main.cpp)...' -ForegroundColor Yellow
$covConsole = Find-CoverageConsole
if (-not $covConsole) {
    throw 'Microsoft.CodeCoverage.Console.exe not found (requires VS Enterprise/coverage workload). Or pass -SkipCoverage.'
}

$covDir = Join-Path $outDir 'coverage'
if (Test-Path $covDir) { Remove-Item -Recurse -Force $covDir }
New-Item -ItemType Directory -Force -Path $covDir | Out-Null
$cobertura = Join-Path $covDir 'cobertura.xml'
$sources = Join-Path $RepoRoot 'redcli'

Write-Host "  Coverage tool: $covConsole" -ForegroundColor Gray
$covOut = Join-Path $covDir 'collect_stdout.txt'
$covErr = Join-Path $covDir 'collect_stderr.txt'
$covSettings = Join-Path $RepoRoot 'tests\coverage.settings.xml'
$covArgs = @(
    'collect'
    '--output', $cobertura
    '--output-format', 'cobertura'
    '--settings', $covSettings
    '--nologo'
    '--'
    $testExe
)
$covProc = Start-Process -FilePath $covConsole `
    -ArgumentList $covArgs `
    -WorkingDirectory (Split-Path $testExe -Parent) `
    -Wait -PassThru -NoNewWindow `
    -RedirectStandardOutput $covOut `
    -RedirectStandardError $covErr
if (Test-Path $covOut) { Get-Content -LiteralPath $covOut | Write-Host }
if (Test-Path $covErr) { Get-Content -LiteralPath $covErr | ForEach-Object { Write-Host $_ -ForegroundColor DarkGray } }
if ($covProc.ExitCode -ne 0) { throw "Coverage collect failed (exit $($covProc.ExitCode))" }
if (-not (Test-Path $cobertura)) { throw "Cobertura report missing: $cobertura" }

$linePct = Get-CoberturaLibraryLinePct -XmlPath $cobertura -SourceRoot $sources
$linePctRounded = [math]::Round($linePct, 2)
Write-Host ("  Library line coverage: {0:N2}% (gate {1}%)" -f $linePctRounded, $MinCoverage) -ForegroundColor Cyan
Write-Host "  Report: $cobertura" -ForegroundColor Gray

$summaryPath = Join-Path $outDir 'coverage_summary.txt'
@"
line_coverage_pct=$linePctRounded
min_required=$MinCoverage
sources=redcli (excluding main.cpp)
report=$cobertura
"@ | Set-Content -LiteralPath $summaryPath -Encoding UTF8

if ($linePctRounded + 0.0001 -lt $MinCoverage) {
    throw ("Coverage gate FAILED: {0:N2}% < {1}%" -f $linePctRounded, $MinCoverage)
}

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host (" Unit tests + coverage PASS ({0:N2}%)" -f $linePctRounded) -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan
