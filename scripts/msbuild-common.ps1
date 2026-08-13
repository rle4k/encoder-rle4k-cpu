# Shared helpers for encoder-rle4k-cpu build and test scripts.

function Get-RepoRoot {
    param([string]$StartPath = $PSScriptRoot)
    if ((Split-Path -Leaf $StartPath) -eq 'scripts') {
        return (Split-Path -Parent $StartPath)
    }
    return $StartPath
}

function Get-Rle4kBuildTempDir {
    param([string]$RepoRoot)
    $dir = Join-Path $RepoRoot 'build\temp'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

function Get-Rle4kBuildOutDir {
    param([string]$RepoRoot)
    $dir = Join-Path $RepoRoot 'build\out'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

function Write-Rle4kTempLogPath {
    param(
        [string]$RepoRoot,
        [string]$FileName
    )
    return Join-Path (Get-Rle4kBuildTempDir -RepoRoot $RepoRoot) $FileName
}

function Invoke-WithTempLog {
    param(
        [string]$RepoRoot,
        [string]$LogFileName,
        [scriptblock]$Command
    )

    $logPath = Write-Rle4kTempLogPath -RepoRoot $RepoRoot -FileName $LogFileName
    Write-Host "  log: $logPath" -ForegroundColor Gray

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @()
        & $Command 2>&1 | ForEach-Object {
            $text = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.ToString() } else { [string]$_ }
            $lines += $text
            Write-Host $text
        }
        if ($lines.Count -gt 0) {
            $lines | Set-Content -Path $logPath -Encoding UTF8
        } else {
            Set-Content -Path $logPath -Value '' -Encoding UTF8
        }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }
}

function Test-WindowsDriveReady {
    param([string]$Path)
    if (-not $Path) { return $false }
    # Join-Path / Test-Path throw DriveNotFoundException when the drive letter
    # is missing (CI hosts often have D: but not E:).
    if ($Path -match '^[A-Za-z]:') {
        $letter = $Path.Substring(0, 1)
        return [bool](Get-PSDrive -Name $letter -ErrorAction SilentlyContinue)
    }
    return $true
}

function Test-PathSafe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [ValidateSet('Any', 'Container', 'Leaf')]$PathType = 'Any'
    )
    if (-not (Test-WindowsDriveReady -Path $Path)) { return $false }
    try {
        return [bool](Test-Path -LiteralPath $Path -PathType $PathType -ErrorAction Stop)
    } catch {
        return $false
    }
}

function Test-IsVsBundledVcpkg {
    param([string]$Root)
    if (-not $Root) { return $false }
    # VS 2022 ships a vcpkg under VC\vcpkg — usually no x64-windows-static installs.
    return ($Root -match '(?i)[\\/]VC[\\/]vcpkg$')
}

function Resolve-VcpkgRoot {
    # 1) Explicit env (CI host injects VCPKG_ROOT=D:\vcpkg via runner config.yaml).
    if ($env:VCPKG_ROOT -and -not (Test-IsVsBundledVcpkg $env:VCPKG_ROOT) `
            -and (Test-PathSafe "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -PathType Leaf)) {
        return $env:VCPKG_ROOT
    }

    # 2) Well-known full trees BEFORE `vcpkg root` (PATH often points at VS VC\vcpkg).
    foreach ($candidate in @(
            'D:\vcpkg',
            "$env:USERPROFILE\vcpkg",
            'C:\vcpkg',
            'E:\vcpkg'
        )) {
        if ($candidate -and (Test-PathSafe "$candidate\scripts\buildsystems\vcpkg.cmake" -PathType Leaf)) {
            $env:VCPKG_ROOT = $candidate
            return $candidate
        }
    }

    # 3) `vcpkg root` last; reject VS-bundled tree.
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgCmd) {
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $root = (& vcpkg root 2>$null)
        $ErrorActionPreference = $prevEap
        if ($root) { $root = $root.Trim() }
        if ($root -and -not (Test-IsVsBundledVcpkg $root) `
                -and (Test-PathSafe "$root\scripts\buildsystems\vcpkg.cmake" -PathType Leaf)) {
            $env:VCPKG_ROOT = $root
            return $root
        }
    }

    return $null
}

function Clear-InheritedGitDirEnv {
    # act_runner / workflow `git init` often exports GIT_DIR=.git (relative).
    # That breaks `git -C $VCPKG_ROOT show <baseline>:versions/baseline.json`
    # with: fatal: not a git repository: '.git'
    foreach ($name in @(
            'GIT_DIR',
            'GIT_WORK_TREE',
            'GIT_INDEX_FILE',
            'GIT_COMMON_DIR',
            'GIT_OBJECT_DIRECTORY',
            'GIT_ALTERNATE_OBJECT_DIRECTORIES'
        )) {
        if (Test-Path "Env:$name") {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        }
    }
}

function Ensure-VcpkgGitSafeDirectoryConfig {
    # CI host: daemon/SYSTEM SID != owner of D:\vcpkg → dubious ownership.
    # Point GIT_CONFIG_GLOBAL at a shared file so vcpkg's git calls succeed too.
    if (-not $env:GIT_CONFIG_GLOBAL) {
        $env:GIT_CONFIG_GLOBAL = 'D:\gitea-runner\gitconfig'
    }
    $cfg = $env:GIT_CONFIG_GLOBAL
    $parent = Split-Path -Parent $cfg
    if ($parent -and (Test-WindowsDriveReady -Path $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        if (-not (Test-PathSafe $cfg -PathType Leaf)) {
            @(
                '[safe]'
                "`tdirectory = *"
                "`tdirectory = D:/vcpkg"
            ) | Set-Content -LiteralPath $cfg -Encoding ascii
        }
    }
}

function Invoke-GitAt {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$GitArgs
    )
    Ensure-VcpkgGitSafeDirectoryConfig
    # -c covers cases where GIT_CONFIG_GLOBAL path is unavailable.
    & git -c safe.directory=* -C $Root @GitArgs
    return $LASTEXITCODE
}

function Get-VcpkgBuiltinBaseline {
    param([string]$RepoRoot)
    $manifest = Join-Path $RepoRoot 'vcpkg.json'
    if (-not (Test-PathSafe $manifest -PathType Leaf)) { return $null }
    $raw = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8
    if ($raw -match '"builtin-baseline"\s*:\s*"([0-9a-fA-F]{40})"') {
        return $Matches[1]
    }
    return $null
}

function Assert-VcpkgRootSupportsManifestBaseline {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Baseline
    )
    Clear-InheritedGitDirEnv
    Ensure-VcpkgGitSafeDirectoryConfig

    $gitMeta = Join-Path $Root '.git'
    if (-not (Test-PathSafe $gitMeta)) {
        throw @"
VCPKG_ROOT=$Root is not a git clone (missing .git).
Manifest mode needs ``git show <baseline>:versions/baseline.json`` inside that tree.

  git clone https://github.com/microsoft/vcpkg.git $Root
  git -C $Root checkout $Baseline
  & `"$Root\bootstrap-vcpkg.bat`"
"@
    }

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $insideOut = & git -c safe.directory=* -C $Root rev-parse --is-inside-work-tree 2>&1
    $insideRc = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    if ($insideRc -ne 0) {
        throw @"
VCPKG_ROOT=$Root git rev-parse failed (exit $insideRc): $insideOut
If you see 'dubious ownership', set GIT_CONFIG_GLOBAL to a gitconfig with [safe] directory = *.
"@
    }

    if ($Baseline) {
        $ErrorActionPreference = 'Continue'
        $catOut = & git -c safe.directory=* -C $Root cat-file -e "${Baseline}^{commit}" 2>&1
        $catRc = $LASTEXITCODE
        $ErrorActionPreference = $prevEap
        if ($catRc -ne 0) {
            throw @"
VCPKG_ROOT=$Root does not contain baseline commit $Baseline (or git refused the repo).
Detail: $catOut

  git -c safe.directory=* -C $Root fetch --all
  git -c safe.directory=* -C $Root fetch origin $Baseline
"@
        }
    }
}

function Require-VcpkgRoot {
    Clear-InheritedGitDirEnv
    Ensure-VcpkgGitSafeDirectoryConfig
    $root = Resolve-VcpkgRoot
    if (-not $root) {
        throw @"
vcpkg not found. Compression libraries are supplied via vcpkg manifest mode.

  1. git clone https://github.com/microsoft/vcpkg.git
  2. cd vcpkg && .\bootstrap-vcpkg.bat
  3. `$env:VCPKG_ROOT = (Get-Location).Path

See README.md and docs/DEPENDENCIES.md.
"@
    }

    $repoRoot = Get-RepoRoot -StartPath $PSScriptRoot
    $baseline = Get-VcpkgBuiltinBaseline -RepoRoot $repoRoot
    Assert-VcpkgRootSupportsManifestBaseline -Root $root -Baseline $baseline
    return $root
}

function Test-VcpkgStaticTcmalloc {
    param([string]$Root)
    if (-not $Root) { return $false }
    if (-not (Test-WindowsDriveReady -Path $Root)) { return $false }
    try {
        $lib = Join-Path $Root.TrimEnd('\') 'installed\x64-windows-static\lib\libtcmalloc_minimal.lib'
        if (-not (Test-PathSafe $lib -PathType Leaf)) { return $false }
        # Reject DLL import libs (~70KB); static Override archive is ~2–3MB.
        return ((Get-Item -LiteralPath $lib).Length -ge 500000)
    } catch {
        return $false
    }
}

function Resolve-VcpkgRootForTcmalloc {
    # Prefer project-local installed tree, then env / MS fallbacks (vcpkg-local-preference).
    $repoRoot = Get-RepoRoot -StartPath $PSScriptRoot
    $ezdiRoot = $null
    try {
        $ezdiRoot = Split-Path (Split-Path $repoRoot -Parent) -Parent
    } catch {
        $ezdiRoot = $null
    }
    $candidates = @()
    if ($env:VCPKG_LOCAL_ROOT) { $candidates += $env:VCPKG_LOCAL_ROOT }
    if ($ezdiRoot -and (Test-WindowsDriveReady -Path $ezdiRoot)) {
        $candidates += (Join-Path $ezdiRoot 'vcpkg')
    }
    if ($env:VCPKG_ROOT -and -not (Test-IsVsBundledVcpkg $env:VCPKG_ROOT)) {
        $candidates += $env:VCPKG_ROOT
    }
    # Prefer D: over E: (CI hosts often lack E:).
    $candidates += @('D:\vcpkg', 'E:\vcpkg')

    foreach ($cand in $candidates) {
        if (-not $cand) { continue }
        if (-not (Test-WindowsDriveReady -Path $cand)) { continue }
        if (Test-VcpkgStaticTcmalloc -Root $cand) {
            try {
                return (Resolve-Path -LiteralPath $cand).Path
            } catch {
                continue
            }
        }
    }
    return $null
}

function Ensure-VcpkgStaticTcmalloc {
    $root = Resolve-VcpkgRootForTcmalloc
    if ($root) {
        $lib = Join-Path $root 'installed\x64-windows-static\lib\libtcmalloc_minimal.lib'
        Write-Host "  tcmalloc: static $lib" -ForegroundColor Green
        return $root
    }

    # Try to install via MS vcpkg tool + local overlay ports when possible.
    $toolRoot = Require-VcpkgRoot
    $repoRoot = Get-RepoRoot -StartPath $PSScriptRoot
    $ezdiRoot = Split-Path (Split-Path $repoRoot -Parent) -Parent
    $overlay = Join-Path $ezdiRoot 'vcpkg\ports'
    Write-Host "  tcmalloc: installing gperftools:x64-windows-static via $toolRoot ..." -ForegroundColor Yellow
    $prevOverlay = $env:VCPKG_OVERLAY_PORTS
    try {
        if (Test-Path $overlay) {
            $env:VCPKG_OVERLAY_PORTS = $overlay
        }
        & "$toolRoot\vcpkg.exe" install gperftools:x64-windows-static
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install gperftools:x64-windows-static failed (exit $LASTEXITCODE)"
        }
    } finally {
        if ($null -eq $prevOverlay) {
            Remove-Item Env:VCPKG_OVERLAY_PORTS -ErrorAction SilentlyContinue
        } else {
            $env:VCPKG_OVERLAY_PORTS = $prevOverlay
        }
    }

    $root = Resolve-VcpkgRootForTcmalloc
    if (-not $root) {
        throw @"
vcpkg static libtcmalloc_minimal.lib not found under VCPKG_LOCAL_ROOT / project vcpkg / VCPKG_ROOT / D:\vcpkg / E:\vcpkg.
Set VCPKG_ROOT to a full Microsoft vcpkg tree (recommended: D:\vcpkg), then:
  vcpkg install gperftools[override]:x64-windows-static
"@
    }
    $lib = Join-Path $root 'installed\x64-windows-static\lib\libtcmalloc_minimal.lib'
    Write-Host "  tcmalloc: static $lib" -ForegroundColor Green
    return $root
}

function Get-EzdiSharedRoot {
    if ($env:LOCAL_SHARED_ROOT -and (Test-Path (Join-Path $env:LOCAL_SHARED_ROOT 'shared.output-paths.ps1'))) {
        return (Resolve-Path $env:LOCAL_SHARED_ROOT).Path
    }
    if ($env:EZDI_ROOT -and (Test-Path (Join-Path $env:EZDI_ROOT 'shared\shared.output-paths.ps1'))) {
        return (Resolve-Path (Join-Path $env:EZDI_ROOT 'shared')).Path
    }

    # Repo lives at <ezdi>/papers/encoder-rle4k-cpu — two parents up is monorepo root.
    $repoRoot = Get-RepoRoot -StartPath $PSScriptRoot
    $monorepoShared = Join-Path (Split-Path (Split-Path $repoRoot -Parent) -Parent) 'shared'
    if (Test-Path (Join-Path $monorepoShared 'shared.output-paths.ps1')) {
        return (Resolve-Path $monorepoShared).Path
    }

    return $null
}

function Test-Rle4kDepsVendorComplete {
    param([string]$RepoRoot = '')
    if (-not $RepoRoot) { $RepoRoot = Get-RepoRoot -StartPath $PSScriptRoot }
    $versions = Join-Path $RepoRoot 'deps\versions.json'
    if (-not (Test-Path $versions)) { return $false }
    $required = @(
        'zlib\1.3.1\lib\zlib.lib',
        'lzo\2.10\lib\lzo2.lib',
        'snappy\1.2.2\lib\snappy.lib',
        'zstd\1.5.7\lib\zstd.lib',
        'brotli\1.1.0\lib\brotlienc.lib',
        'gperftools\2.16\lib\libtcmalloc_minimal.lib',
        'librle4k\1.0.0\lib\librle4k.lib',
        'libdif\1.0.0\lib\libdif.lib',
        'libraster\1.0.0\lib\libraster.lib'
    )
    # Prefer pins from versions.json when present
    try {
        $pins = Get-Content $versions -Raw | ConvertFrom-Json
        $map = @{
            zlib = 'zlib.lib'
            lzo = 'lzo2.lib'
            snappy = 'snappy.lib'
            zstd = 'zstd.lib'
            brotli = 'brotlienc.lib'
            gperftools = 'libtcmalloc_minimal.lib'
            librle4k = 'librle4k.lib'
            libdif = 'libdif.lib'
            libraster = 'libraster.lib'
        }
        foreach ($key in $map.Keys) {
            $ver = $pins.$key
            if (-not $ver) { return $false }
            $libPath = Join-Path $RepoRoot "deps\$key\$ver\lib\$($map[$key])"
            if (-not (Test-Path $libPath)) { return $false }
        }
        return $true
    } catch {
        foreach ($rel in $required) {
            if (-not (Test-Path (Join-Path $RepoRoot "deps\$rel"))) { return $false }
        }
        return $true
    }
}

function Get-VcpkgCmakeArguments {
    param(
        [ValidateSet('Debug', 'Release')]
        [string]$BuildType = 'Release'
    )

    $repoRoot = Get-RepoRoot -StartPath $PSScriptRoot
    if (Test-Rle4kDepsVendorComplete -RepoRoot $repoRoot) {
        Write-Host '  cmake deps: using deps/ only (skipping vcpkg toolchain)' -ForegroundColor Green
        return @()
    }

    $root = Require-VcpkgRoot
    $toolchain = ($root -replace '\\', '/') + '/scripts/buildsystems/vcpkg.cmake'
    $args = @(
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DVCPKG_TARGET_TRIPLET=x64-windows-static'
    )
    $overlayDir = Join-Path $repoRoot 'vcpkg-overlays'
    if (Test-Path $overlayDir) {
        $overlay = ($overlayDir -replace '\\', '/')
        $args += "-DVCPKG_OVERLAY_PORTS=$overlay"
    }

    # Ensure static tcmalloc is available; CMake resolves the .lib from VCPKG trees.
    [void](Ensure-VcpkgStaticTcmalloc)

    return $args
}

function Test-VisualStudioInstalled {
    param([string]$VersionRange)

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }

    $vsPath = & $vswhere -latest -version $VersionRange -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    return [bool]$vsPath
}

function Get-VcVarsPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        foreach ($versionRange in @('[17.0,18.0)', '[16.0,17.0)')) {
            $vsPath = & $vswhere -latest -version $versionRange -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath 2>$null
            if ($vsPath) {
                $p = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
                if (Test-Path $p) { return $p }
            }
        }
    }

    foreach ($year in @('2022', '2019')) {
        $base = if ($year -eq '2019') { ${env:ProgramFiles(x86)} } else { $env:ProgramFiles }
        foreach ($ed in @('Enterprise', 'Professional', 'Community', 'BuildTools')) {
            $p = Join-Path $base "Microsoft Visual Studio\$year\$ed\VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $p) { return $p }
        }
    }

    return $null
}

function Repair-ProcessPathForVcVars {
    foreach ($name in @('PATH', 'PATH_AGENTS', '__VSCMD_PREINIT_PATH')) {
        $raw = [Environment]::GetEnvironmentVariable($name)
        if (-not $raw) { continue }

        $clean = ($raw -split ';' |
            ForEach-Object { $_.Trim().Trim('"') } |
            Where-Object { $_ }) -join ';'
        [Environment]::SetEnvironmentVariable($name, $clean)
    }
}

function Import-VsDevEnvironment {
    param([string]$VcVarsPath)

    # If a VS build environment is already present in this session (e.g. the
    # user launched a "Developer PowerShell for VS 2022" or already ran vcvars),
    # reuse it instead of re-initializing. Re-running vcvars64.bat on top of an
    # existing VS environment can produce an empty `set` capture and fail.
    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($cl) {
        Write-Host '  vcvars: already initialized in current session (cl.exe on PATH), reusing' -ForegroundColor Green
        return
    }

    Repair-ProcessPathForVcVars

    # Capture the resulting environment to a temp file. This is more robust than
    # capturing `cmd /c "... && set"` output in-memory, which can come back empty
    # when the calling PowerShell session carries stale VS/agent environment vars.
    $tmpEnv = Join-Path $env:TEMP ("rle4k_vsvars_" + [guid]::NewGuid().ToString('N') + '.txt')
    cmd /c "call `"$VcVarsPath`" >nul 2>&1 && set > `"$tmpEnv`"" 2>&1 | Out-Null

    if (-not (Test-Path $tmpEnv) -or (Get-Content $tmpEnv -Raw).Trim().Length -eq 0) {
        # Surface the real vcvars error instead of failing with a generic message.
        Write-Host '  [ERROR] vcvars64.bat did not initialize a usable environment.' -ForegroundColor Red
        Write-Host '  [ERROR] Raw vcvars64.bat output:' -ForegroundColor Red
        cmd /c "call `"$VcVarsPath`"" 2>&1 | ForEach-Object { Write-Host "    $_" }
        if (Test-Path $tmpEnv) { Remove-Item -Force $tmpEnv -ErrorAction SilentlyContinue }
        throw "vcvars64.bat failed: $VcVarsPath"
    }

    Get-Content $tmpEnv | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
        }
    }
    Remove-Item -Force $tmpEnv -ErrorAction SilentlyContinue
}

function Get-CmakeVersionTuple {
    param([string]$CmakePath)
    if (-not $CmakePath -or -not (Test-Path $CmakePath)) { return $null }
    $line = & $CmakePath --version 2>&1 | Select-Object -First 1
    if ($line -notmatch '(\d+)\.(\d+)\.(\d+)') { return $null }
    return [tuple]::Create([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
}

function Get-CmakeExecutable {
    # Prefer CMake 3.21+ (required by current vcpkg compiler detection).
    $candidates = New-Object System.Collections.Generic.List[string]

    $fromPath = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ($fromPath) { [void]$candidates.Add($fromPath) }

    foreach ($vsEdition in @('Enterprise', 'Professional', 'Community', 'BuildTools')) {
        $vsCmake = "C:\Program Files\Microsoft Visual Studio\2022\$vsEdition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $vsCmake) { [void]$candidates.Add($vsCmake) }
    }

    $fallback = 'C:\Program Files\CMake\bin\cmake.exe'
    if (Test-Path $fallback) { [void]$candidates.Add($fallback) }

    $best = $null
    $bestVer = $null
    foreach ($c in ($candidates | Select-Object -Unique)) {
        $ver = Get-CmakeVersionTuple -CmakePath $c
        if (-not $ver) { continue }
        if (-not $bestVer -or
            $ver.Item1 -gt $bestVer.Item1 -or
            ($ver.Item1 -eq $bestVer.Item1 -and $ver.Item2 -gt $bestVer.Item2) -or
            ($ver.Item1 -eq $bestVer.Item1 -and $ver.Item2 -eq $bestVer.Item2 -and $ver.Item3 -gt $bestVer.Item3)) {
            $best = $c
            $bestVer = $ver
        }
    }

    if (-not $best) { return $null }

    if ($bestVer.Item1 -lt 3 -or ($bestVer.Item1 -eq 3 -and $bestVer.Item2 -lt 21)) {
        Write-Host "  [WARN] CMake $($bestVer.Item1).$($bestVer.Item2).$($bestVer.Item3) found; vcpkg needs 3.21+." -ForegroundColor Yellow
        Write-Host '         Install a newer CMake or use the copy bundled with VS 2022.' -ForegroundColor Yellow
    }

    return $best
}

function Get-CmakeGeneratorArguments {
    param(
        [string]$CmakePath,
        [string]$BuildType = 'Release'
    )

    $genHelp = & $CmakePath --help 2>&1 | Out-String
    $cmakeVersion = ((& $CmakePath --version 2>&1 | Select-Object -First 1) -as [string])
    $hasVs2022 = Test-VisualStudioInstalled -VersionRange '[17.0,18.0)'
    $hasVs2019 = Test-VisualStudioInstalled -VersionRange '[16.0,17.0)'

    # Fallback when vswhere is unavailable/empty but vcvars64.bat exists (common on some hosts).
    if (-not $hasVs2022 -and -not $hasVs2019) {
        $vcvars = Get-VcVarsPath
        if ($vcvars -and ($vcvars -match '\\2022\\')) { $hasVs2022 = $true }
        elseif ($vcvars -and ($vcvars -match '\\2019\\')) { $hasVs2019 = $true }
        elseif (Test-Path "${env:ProgramFiles}\Microsoft Visual Studio\2022") { $hasVs2022 = $true }
    }

    # Prefer the VS generator only when vswhere resolves a *matching* instance.
    # Empty/mismatched catalogs (e.g. only VS 18 BuildTools) break the VS 17 generator;
    # NMake + loaded vcvars64.bat is the reliable fallback on those hosts.
    $vswhereHas17 = $false
    $vswhereHas16 = $false
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $p17 = & $vswhere -latest -version '[17.0,18.0)' -products * -property installationPath 2>$null
        $p16 = & $vswhere -latest -version '[16.0,17.0)' -products * -property installationPath 2>$null
        if ($p17) { $vswhereHas17 = $true }
        if ($p16) { $vswhereHas16 = $true }
    }

    if (($genHelp -match 'Visual Studio 17 2022') -and $hasVs2022 -and $vswhereHas17) {
        Write-Host '  generator: Visual Studio 17 2022 (x64)' -ForegroundColor Green
        return @{
            Generator   = 'Visual Studio 17 2022'
            ExtraArgs   = @('-A', 'x64')
            MultiConfig = $true
        }
    }

    if (($genHelp -match 'Visual Studio 16 2019') -and $hasVs2019 -and $vswhereHas16) {
        $extra = @('-A', 'x64')
        if ($hasVs2022) {
            $extra += @('-T', 'v143')
            Write-Host '  generator: Visual Studio 16 2019 + v143 toolset' -ForegroundColor Gray
        } else {
            Write-Host '  generator: Visual Studio 16 2019 (x64)' -ForegroundColor Green
        }
        return @{
            Generator   = 'Visual Studio 16 2019'
            ExtraArgs   = $extra
            MultiConfig = $true
        }
    }

    if ($hasVs2022 -or $hasVs2019) {
        Write-Host "  generator: NMake Makefiles ($BuildType) [vcvars fallback]" -ForegroundColor Green
        return @{
            Generator   = 'NMake Makefiles'
            ExtraArgs   = @("-DCMAKE_BUILD_TYPE=$BuildType")
            MultiConfig = $false
        }
    }

    throw 'No compatible Visual Studio installation found for CMake.'
}

function Find-RedcliExecutable {
    param(
        [string]$RepoRoot,
        [string]$BuildType = 'Release'
    )

    foreach ($candidate in @(
            (Join-Path $RepoRoot "build\$BuildType\redcli.exe"),
            (Join-Path $RepoRoot "build\redcli\$BuildType\redcli.exe"),
            (Join-Path $RepoRoot 'build\redcli.exe')
        )) {
        if (Test-Path $candidate) { return $candidate }
    }

    $found = Get-ChildItem -Path (Join-Path $RepoRoot 'build') -Recurse -Filter redcli.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { return $found.FullName }

    return $null
}

function Copy-RedcliRuntimeFiles {
    param(
        [string]$RepoRoot,
        [string]$BuildType = 'Release'
    )

    $buildDir = Join-Path $RepoRoot 'build'
    foreach ($subdir in @($BuildType, "redcli\$BuildType", '.')) {
        $destDir = Join-Path $buildDir $subdir
        $exe = Join-Path $destDir 'redcli.exe'
        if (-not (Test-Path $exe)) { continue }

        $cfg = Join-Path $destDir 'redcli.json'
        if (Test-Path $cfg) {
            Write-Host "  redcli.json already present: $destDir\" -ForegroundColor DarkGray
            continue
        }

        Push-Location $destDir
        try {
            & $exe init
            if ($LASTEXITCODE -eq 0 -and (Test-Path $cfg)) {
                Write-Host "  redcli.json <- redcli init (built-in) -> $destDir\" -ForegroundColor Green
            } else {
                Write-Host "  [WARN] redcli init failed in $destDir\" -ForegroundColor Yellow
            }
        } finally {
            Pop-Location
        }
    }
}

function Invoke-CmakeBuild {
    param(
        [string]$CmakePath,
        [string]$BuildDir,
        [hashtable]$GenInfo,
        [string]$BuildType = 'Release'
    )

    Push-Location $BuildDir
    try {
        if ($GenInfo.MultiConfig) {
            & $CmakePath --build . --config $BuildType --parallel
        } else {
            & $CmakePath --build . --parallel
        }
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

function Test-TcpPortOpen {
    param(
        [string]$HostName = '127.0.0.1',
        [int]$Port
    )
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect($HostName, $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(400)
        if ($ok -and $client.Connected) {
            $client.EndConnect($iar) | Out-Null
            $client.Close()
            return $true
        }
        $client.Close()
    } catch {
    }
    return $false
}

function Ensure-VcpkgDownloadProxy {
    # vcpkg fetches GitHub distfiles via curl. On hosts where github.com:443 is
    # blocked/flaky but a local SOCKS5 (Clash/V2Ray) listens on 127.0.0.1:1089,
    # publish proxy env vars so manifest installs can proceed.
    #
    # Respect an explicit user proxy unless it points at a dead local port.
    $existing = @($env:ALL_PROXY, $env:all_proxy, $env:HTTPS_PROXY, $env:https_proxy, $env:HTTP_PROXY, $env:http_proxy) |
        Where-Object { $_ } |
        Select-Object -First 1

    if ($existing) {
        if ($existing -match '(?i)(?:127\.0\.0\.1|localhost):(\d+)') {
            $port = [int]$Matches[1]
            if (-not (Test-TcpPortOpen -Port $port)) {
                Write-Host "  vcpkg proxy: clearing unreachable $existing" -ForegroundColor Yellow
                foreach ($name in @('ALL_PROXY', 'all_proxy', 'HTTPS_PROXY', 'https_proxy', 'HTTP_PROXY', 'http_proxy')) {
                    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
                }
                $existing = $null
            }
        }
    }

    if ($existing) {
        Write-Host "  vcpkg proxy: using existing ($existing)" -ForegroundColor Green
        return
    }

    $socksPort = 1089
    if ($env:RLE4K_SOCKS_PROXY_PORT) {
        $socksPort = [int]$env:RLE4K_SOCKS_PROXY_PORT
    }
    if (-not (Test-TcpPortOpen -Port $socksPort)) {
        Write-Host "  vcpkg proxy: none (SOCKS 127.0.0.1:$socksPort not listening)" -ForegroundColor Gray
        return
    }

    $proxy = "socks5h://127.0.0.1:$socksPort"
    $env:ALL_PROXY = $proxy
    $env:all_proxy = $proxy
    $env:HTTPS_PROXY = $proxy
    $env:https_proxy = $proxy
    $env:HTTP_PROXY = $proxy
    $env:http_proxy = $proxy
    Write-Host "  vcpkg proxy: $proxy (GitHub distfile fallback)" -ForegroundColor Green
}

function Initialize-Rle4kBuildEnvironment {
    $vcvars = Get-VcVarsPath
    if (-not $vcvars) {
        throw 'Visual Studio with C++ tools not found. Install VS 2022 (or Build Tools) with the MSVC workload.'
    }

    Write-Host "  vcvars: $vcvars" -ForegroundColor Green
    Import-VsDevEnvironment -VcVarsPath $vcvars

    $cmake = Get-CmakeExecutable
    if (-not $cmake) {
        throw 'cmake not found. Install CMake 3.21+ (or use the CMake bundled with VS 2022) and ensure it is discoverable.'
    }

    Write-Host "  cmake: $cmake" -ForegroundColor Green

    # vcpkg's internal compiler detection invokes `cmake` from PATH; keep the
    # selected 3.21+ binary ahead of any older install (e.g. 3.16).
    $cmakeDir = Split-Path -Parent $cmake
    $env:PATH = "$cmakeDir;$env:PATH"
    $env:CMAKE = $cmake

    Ensure-VcpkgDownloadProxy

    return $cmake
}

function Write-VcpkgStatus {
    $root = Require-VcpkgRoot
    Write-Host "  vcpkg: $root" -ForegroundColor Green
}

function Invoke-Rle4kCompileProbe {
    param(
        [string]$RepoRoot,
        [string]$SourceRelativePath,
        [string]$LogFileName = 'rasterizer_errors.txt'
    )

    $logPath = Write-Rle4kTempLogPath -RepoRoot $RepoRoot -FileName $LogFileName
    $src = Join-Path $RepoRoot $SourceRelativePath
    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $cl) {
        Set-Content -Path $logPath -Value 'cl.exe not found in PATH'
        return $false
    }

    Push-Location $RepoRoot
    try {
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & cl.exe /c /EHsc /std:c++17 /W3 $SourceRelativePath 2>&1 |
            Tee-Object -FilePath $logPath |
            Out-Null
        $rc = $LASTEXITCODE
        $ErrorActionPreference = $prevEap
        Add-Content -Path $logPath -Value "EXIT_CODE=$rc"
        return ($rc -eq 0)
    } finally {
        Pop-Location
    }
}

function Invoke-RedcliBenchmark {
    param(
        [string]$Redcli,
        [string]$InputPath,
        [string]$CsvPath,
        [int]$Repeat = 3,
        [switch]$Verbose
    )

    $args = @('run', '--mode', '0', '--input', $InputPath, '--repeat', "$Repeat")
    if ($Verbose) { $args += @('--verbose', 'file') }

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Redcli @args 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    return $rc
}

function Ensure-Rle4kGenTestData {
    param(
        [string]$RepoRoot,
        [int]$StripeCount = 5,
        [string]$OutputPath = ''
    )

    if (-not $OutputPath) {
        $OutputPath = Join-Path $RepoRoot 'data\test_stripes.bin'
    }

    $genExe = Join-Path $RepoRoot 'tools\rle4k_gen_test_data.exe'

    if (-not (Test-Path $genExe)) {
        $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
        if (-not $cl) {
            Write-Host '  [WARN] cl.exe not found; cannot compile rle4k_gen_test_data.exe' -ForegroundColor Yellow
            return $false
        }

        Write-Host '  Compiling tools\rle4k_gen_test_data.exe...' -ForegroundColor Gray
        Push-Location (Join-Path $RepoRoot 'tools')
        try {
            $prevEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            & cl.exe /EHsc /std:c++17 /O2 /Fe:rle4k_gen_test_data.exe rle4k_gen_test_data.cpp 2>&1 | Out-Null
            $ErrorActionPreference = $prevEap
            if ($LASTEXITCODE -ne 0) { return $false }
        } finally {
            Pop-Location
        }
    }

    $dataDir = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    & $genExe $StripeCount $OutputPath
    return (Test-Path $OutputPath)
}
