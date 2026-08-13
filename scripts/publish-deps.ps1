#Requires -Version 5.1
<#
.SYNOPSIS
  Build base libraries and vendor former-vcpkg packages into encoder-rle4k-cpu/deps/.

.DESCRIPTION
  One-click publish for libdif, libraster, librle4k (evaluation gate ON by default)
  plus zlib/lzo/snappy/zstd/brotli/gperftools/gtest harvested from a developer-machine
  MS vcpkg tree (E:\vcpkg or VCPKG_ROOT). Each deps/<name>/<version>/ holds headers,
  static .lib, README/VERSION/LICENSE only — no .cpp/.c source trees.
  Package/folder names never use a "trial" suffix.

.PARAMETER Configuration
  CMake build config (default Release).

.PARAMETER ExpiryDate
  Optional YYYY-MM-DD. If omitted: max(2027-12-30, BuildUtcDate + 500 days).

.PARAMETER LibVersion
  Semver folder name for base libs (default from each base/*/version.txt or 1.0.0).

.PARAMETER NoGate
  Author-only: omit evaluation macros (same folder naming; undocumented publicly).

.PARAMETER SkipVcpkgVendor
  Skip former-vcpkg packaging.

.PARAMETER SkipBase
  Skip base lib rebuild.

.PARAMETER Libs
  Which base libs to publish (default: all three).
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [string]$ExpiryDate = '',

    [string]$LibVersion = '',

    [switch]$NoGate,

    [switch]$SkipVcpkgVendor,

    [switch]$SkipBase,

    [string[]]$Libs = @('libdif', 'libraster', 'librle4k')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    $here = $PSScriptRoot
    return (Resolve-Path (Join-Path $here '..')).Path
}

function Get-WorkspaceRoot {
    $repo = Get-RepoRoot
    return (Resolve-Path (Join-Path $repo '..')).Path
}

function Get-FloorExpiry {
    return [datetime]::SpecifyKind([datetime]'2027-12-30', [DateTimeKind]::Utc)
}

function Resolve-ExpiryDate {
    param([string]$Explicit)
    $floor = Get-FloorExpiry
    if ($Explicit) {
        if ($Explicit -notmatch '^\d{4}-\d{2}-\d{2}$') {
            throw "ExpiryDate must be YYYY-MM-DD, got: $Explicit"
        }
        return [datetime]::SpecifyKind([datetime]$Explicit, [DateTimeKind]::Utc)
    }
    $buildUtc = [datetime]::UtcNow.Date
    $auto = $buildUtc.AddDays(500)
    if ($auto -gt $floor) { return $auto }
    return $floor
}

function Format-ExpiryYmd {
    param([datetime]$Date)
    return $Date.ToString('yyyyMMdd')
}

function Format-ExpiryIso {
    param([datetime]$Date)
    return $Date.ToString('yyyy-MM-dd')
}

function Get-LibVersion {
    param(
        [string]$BaseLibDir,
        [string]$Override
    )
    if ($Override) { return $Override }
    $vf = Join-Path $BaseLibDir 'version.txt'
    if (Test-Path $vf) {
        $v = (Get-Content $vf -Raw).Trim()
        if ($v) { return $v }
    }
    return '1.0.0'
}

function Find-BuiltLib {
    param(
        [string]$BuildDir,
        [string]$LibName,
        [string]$Configuration
    )
    foreach ($c in @(
            (Join-Path $BuildDir "$Configuration\$LibName.lib"),
            (Join-Path $BuildDir "$LibName.lib"),
            (Join-Path $BuildDir "Release\$LibName.lib")
        )) {
        if (Test-Path -LiteralPath $c) { return $c }
    }
    $found = Get-ChildItem -Path $BuildDir -Filter "$LibName.lib" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

function Publish-BaseLib {
    param(
        [string]$LibName,
        [string]$GateMacroPrefix,
        [string]$WorkspaceRoot,
        [string]$RepoRoot,
        [string]$Configuration,
        [datetime]$Expiry,
        [string]$Version,
        [bool]$EnableGate,
        [string]$CmakePath,
        [hashtable]$GenInfo,
        [scriptblock]$ExtraPublishCopy
    )

    $srcRoot = Join-Path $WorkspaceRoot "base\$LibName"
    if (-not (Test-Path (Join-Path $srcRoot 'CMakeLists.txt'))) {
        throw "Missing $LibName CMakeLists.txt at $srcRoot"
    }

    $buildDir = Join-Path $srcRoot 'build'
    $outRoot = Join-Path $RepoRoot "deps\$LibName\$Version"
    $ymd = Format-ExpiryYmd $Expiry
    $iso = Format-ExpiryIso $Expiry
    $enableFlag = "${GateMacroPrefix}_ENABLE_GATE"
    $expiryFlag = "${GateMacroPrefix}_TRIAL_EXPIRY_YMD"

    Write-Host "=== Publishing $LibName $Version (gate=$EnableGate; expiry=$iso) ==="

    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
    }
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    $cmakeDefs = @(
        '-S', $srcRoot,
        '-B', $buildDir,
        '-G', $GenInfo.Generator
    ) + $GenInfo.ExtraArgs + @(
        "-D${enableFlag}=$([int]$EnableGate)",
        "-D${expiryFlag}=$ymd"
    )

    & $CmakePath @cmakeDefs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for $LibName" }

    if ($GenInfo.MultiConfig) {
        & $CmakePath --build $buildDir --config $Configuration --parallel
    } else {
        & $CmakePath --build $buildDir --parallel
    }
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed for $LibName" }

    $libCandidate = Find-BuiltLib -BuildDir $buildDir -LibName $LibName -Configuration $Configuration
    if (-not $libCandidate) {
        throw "$LibName.lib not found under $buildDir"
    }

    if (Test-Path $outRoot) {
        Remove-Item -Recurse -Force $outRoot
    }
    $includeOut = Join-Path $outRoot 'include'
    $libOut = Join-Path $outRoot 'lib'
    New-Item -ItemType Directory -Force -Path $includeOut, $libOut | Out-Null

    Copy-Item -Recurse (Join-Path $srcRoot 'include\*') $includeOut
    Copy-Item $libCandidate (Join-Path $libOut "$LibName.lib") -Force

    # Common namespace headers (shared.h / cndefs.h) for libdif / libraster consumers
    $commonInclude = Join-Path $WorkspaceRoot 'base\common\include'
    if (Test-Path $commonInclude) {
        Copy-Item -Recurse (Join-Path $commonInclude '*') $includeOut -Force
    }

    # Intentionally do not ship sources under deps/ — reviewable trees stay in base/<name>.

    if (Test-Path (Join-Path $srcRoot 'LICENSE')) {
        Copy-Item (Join-Path $srcRoot 'LICENSE') (Join-Path $outRoot 'LICENSE') -Force
    }

    if ($ExtraPublishCopy) {
        & $ExtraPublishCopy -IncludeOut $includeOut -SrcRoot $srcRoot -OutRoot $outRoot
    }

    $role = switch ($LibName) {
        'librle4k' { 'RLE4K encode/decode only. Does not load DIF or rasterize geometry.' }
        'libdif' { 'DIF parse to cad_document only. Does not rasterize or encode.' }
        'libraster' { 'Rasterize cad_document to bitmap_info only. Does not encode/compare codecs.' }
        default { $LibName }
    }

    $today = (Get-Date).ToString('yyyy-MM-dd')
    $readme = @"
---
title: $LibName $Version
created: $today
author: publish-deps.ps1
last_updated: $today
updated_by: publish-deps.ps1
---

# $LibName $Version

| Field | Value |
|-------|-------|
| Origin | Workspace ``base/$LibName`` |
| Artifact | ``lib/$LibName.lib`` (x64, /MT, $Configuration) |
| Headers | ``include/`` |
| Sources | Not shipped in ``deps/``; reviewable at workspace ``base/$LibName`` |
| Gate | $(if ($EnableGate) { 'embedded (system time + EXE mtime)' } else { 'disabled (author-only build)' }) |

## Role

$role

## Contact

Commercial use or evaluation continuation: yuqp78@foxmail.com
"@
    Set-Content -Path (Join-Path $outRoot 'README.md') -Value $readme -Encoding utf8

    $versionFile = @"
name: $LibName
version: $Version
gate: $(if ($EnableGate) { 'true' } else { 'false' })
triplet: x64-windows-static-mt
"@
    Set-Content -Path (Join-Path $outRoot 'VERSION') -Value $versionFile.Trim() -Encoding utf8

    $hash = (Get-FileHash -Algorithm SHA256 (Join-Path $libOut "$LibName.lib")).Hash
    Write-Host "Published: $outRoot"
    Write-Host "SHA256 ${LibName}.lib: $hash"
    return @{ Name = $LibName; Version = $Version; Path = $outRoot; Sha256 = $hash; Expiry = $iso }
}

function Resolve-VcpkgRootForVendor {
    $candidates = @()
    if ($env:VCPKG_ROOT) { $candidates += $env:VCPKG_ROOT }
    if ($env:VCPKG_LOCAL_ROOT) { $candidates += $env:VCPKG_LOCAL_ROOT }
    $candidates += @('E:\vcpkg', 'D:\vcpkg', 'C:\vcpkg')
    foreach ($c in $candidates) {
        if (-not $c) { continue }
        $libDir = Join-Path $c 'installed\x64-windows-static\lib'
        if (Test-Path $libDir) { return (Resolve-Path $c).Path }
    }
    throw "No vcpkg x64-windows-static install found for vendor step (set VCPKG_ROOT)."
}

function Publish-VcpkgPackage {
    param(
        [string]$RepoRoot,
        [string]$VcpkgRoot,
        [string]$Name,
        [string]$Version,
        [string[]]$LibPatterns,
        [string[]]$HeaderGlobs,
        [string]$LicenseHint
    )

    $installed = Join-Path $VcpkgRoot 'installed\x64-windows-static'
    $pkgDir = Get-ChildItem (Join-Path $VcpkgRoot 'packages') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "${Name}_x64-windows-static*" } |
        Select-Object -First 1

    $outRoot = Join-Path $RepoRoot "deps\$Name\$Version"
    if (Test-Path $outRoot) { Remove-Item -Recurse -Force $outRoot }
    $includeOut = Join-Path $outRoot 'include'
    $libOut = Join-Path $outRoot 'lib'
    New-Item -ItemType Directory -Force -Path $includeOut, $libOut | Out-Null

    $copiedLibs = @()
    foreach ($pat in $LibPatterns) {
        $hits = Get-ChildItem (Join-Path $installed 'lib') -Filter $pat -ErrorAction SilentlyContinue
        foreach ($h in $hits) {
            Copy-Item $h.FullName $libOut -Force
            $copiedLibs += $h.Name
        }
    }
    if ($copiedLibs.Count -eq 0) {
        throw "No libs matched for $Name under $installed\lib ($($LibPatterns -join ', '))"
    }

    # Headers: prefer package share/include, else installed/include selective copy
    $hdrSrc = $null
    if ($pkgDir -and (Test-Path (Join-Path $pkgDir.FullName 'include'))) {
        $hdrSrc = Join-Path $pkgDir.FullName 'include'
    } elseif (Test-Path (Join-Path $installed 'include')) {
        $hdrSrc = Join-Path $installed 'include'
    }

    if ($hdrSrc) {
        foreach ($g in $HeaderGlobs) {
            $full = Join-Path $hdrSrc $g
            if (Test-Path $full) {
                $destParent = Join-Path $includeOut (Split-Path $g -Parent)
                if (-not (Test-Path $destParent) -and (Split-Path $g -Parent)) {
                    New-Item -ItemType Directory -Force -Path $destParent | Out-Null
                }
                if ((Get-Item $full).PSIsContainer) {
                    $leaf = Split-Path $g -Leaf
                    Copy-Item -Recurse $full (Join-Path $includeOut $leaf) -Force
                } else {
                    $dest = Join-Path $includeOut $g
                    $destDir = Split-Path $dest -Parent
                    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Force -Path $destDir | Out-Null }
                    Copy-Item $full $dest -Force
                }
            }
        }
    }

    # Intentionally do not ship sources under deps/ — OSS review is upstream / vcpkg on the author machine.

    # LICENSE / notices only (never copy buildtrees source trees)
    $licCandidates = [System.Collections.Generic.List[string]]::new()
    foreach ($c in @(
            (Join-Path $installed "share\$Name\copyright"),
            (Join-Path $installed "share\$Name\LICENSE"),
            $LicenseHint
        )) {
        if ($c) { [void]$licCandidates.Add($c) }
    }
    $btSrc = Join-Path $VcpkgRoot "buildtrees\$Name\src"
    if (Test-Path $btSrc) {
        $btClean = Get-ChildItem $btSrc -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($btClean) {
            foreach ($n in @('LICENSE', 'LICENSE.txt', 'COPYING')) {
                [void]$licCandidates.Add((Join-Path $btClean.FullName $n))
            }
        }
    }
    $licCopied = $false
    foreach ($cand in $licCandidates) {
        if ($cand -and (Test-Path $cand)) {
            Copy-Item $cand (Join-Path $outRoot 'LICENSE') -Force
            $licCopied = $true
            break
        }
    }
    if (-not $licCopied) {
        Set-Content -Path (Join-Path $outRoot 'LICENSE') -Value "See upstream $Name license; vendored from vcpkg $Version." -Encoding utf8
    }

    $today = (Get-Date).ToString('yyyy-MM-dd')
    $readme = @"
---
title: $Name $Version
created: $today
author: publish-deps.ps1
last_updated: $today
updated_by: publish-deps.ps1
---

# $Name $Version

| Field | Value |
|-------|-------|
| Origin | vcpkg ``x64-windows-static`` ($VcpkgRoot) |
| Artifacts | $($copiedLibs -join ', ') |
| Headers | ``include/`` |
| Sources | Not shipped in ``deps/``; review upstream / author-machine vcpkg buildtrees |
| Triplet | x64-windows-static (/MT) |
| Role | Former tool vcpkg dependency; linked from ``deps/`` only |

Tool builds should not require a live vcpkg install after this package is published.
"@
    Set-Content -Path (Join-Path $outRoot 'README.md') -Value $readme -Encoding utf8
    $versionFile = @"
name: $Name
version: $Version
triplet: x64-windows-static
"@
    Set-Content -Path (Join-Path $outRoot 'VERSION') -Value $versionFile.Trim() -Encoding utf8

    Write-Host "Vendored $Name $Version -> $outRoot ($($copiedLibs -join ', '))"
    return @{ Name = $Name; Version = $Version; Path = $outRoot; Libs = $copiedLibs }
}

function Update-VersionsJson {
    param(
        [string]$RepoRoot,
        [hashtable]$Pins
    )
    $depsDir = Join-Path $RepoRoot 'deps'
    New-Item -ItemType Directory -Force -Path $depsDir | Out-Null
    $path = Join-Path $depsDir 'versions.json'
    $obj = [ordered]@{}
    if (Test-Path $path) {
        $existing = Get-Content $path -Raw | ConvertFrom-Json
        foreach ($p in $existing.PSObject.Properties) {
            $obj[$p.Name] = $p.Value
        }
    }
    foreach ($k in $Pins.Keys) {
        $obj[$k] = $Pins[$k]
    }
    ($obj | ConvertTo-Json -Depth 5) + "`n" | Set-Content -Path $path -Encoding utf8
    Write-Host "Updated $path"
}

# --- main ---
. (Join-Path $PSScriptRoot 'msbuild-common.ps1')

$repoRoot = Get-RepoRoot
$workspaceRoot = Get-WorkspaceRoot
$expiry = Resolve-ExpiryDate -Explicit $ExpiryDate
$enableGate = -not $NoGate.IsPresent

Write-Host "Workspace: $workspaceRoot"
Write-Host "Tool repo: $repoRoot"
Write-Host "Expiry: $(Format-ExpiryIso $expiry) (YMD $(Format-ExpiryYmd $expiry))"
Write-Host "Gate: $enableGate"

$cmake = Initialize-Rle4kBuildEnvironment
$genInfo = Get-CmakeGeneratorArguments -CmakePath $cmake -BuildType $Configuration

$script:results = [System.Collections.Generic.List[object]]::new()
$script:pins = @{}

if (-not $SkipBase) {
    foreach ($lib in $Libs) {
        switch ($lib) {
            'librle4k' {
                $ver = Get-LibVersion -BaseLibDir (Join-Path $workspaceRoot 'base\librle4k') -Override $LibVersion
                $r = Publish-BaseLib -LibName 'librle4k' -GateMacroPrefix 'LIBRLE4K' `
                    -WorkspaceRoot $workspaceRoot -RepoRoot $repoRoot `
                    -Configuration $Configuration -Expiry $expiry -Version $ver -EnableGate $enableGate `
                    -CmakePath $cmake -GenInfo $genInfo
                [void]$script:results.Add($r)
                $script:pins['librle4k'] = $ver
            }
            'libdif' {
                $ver = Get-LibVersion -BaseLibDir (Join-Path $workspaceRoot 'base\libdif') -Override $LibVersion
                $r = Publish-BaseLib -LibName 'libdif' -GateMacroPrefix 'LIBDIF' `
                    -WorkspaceRoot $workspaceRoot -RepoRoot $repoRoot `
                    -Configuration $Configuration -Expiry $expiry -Version $ver -EnableGate $enableGate `
                    -CmakePath $cmake -GenInfo $genInfo
                [void]$script:results.Add($r)
                $script:pins['libdif'] = $ver
            }
            'libraster' {
                $ver = Get-LibVersion -BaseLibDir (Join-Path $workspaceRoot 'base\libraster') -Override $LibVersion
                $r = Publish-BaseLib -LibName 'libraster' -GateMacroPrefix 'LIBRASTER' `
                    -WorkspaceRoot $workspaceRoot -RepoRoot $repoRoot `
                    -Configuration $Configuration -Expiry $expiry -Version $ver -EnableGate $enableGate `
                    -CmakePath $cmake -GenInfo $genInfo `
                    -ExtraPublishCopy {
                        param($IncludeOut, $SrcRoot, $OutRoot)
                        # Also ship libdif public headers so rasterizer.h resolves without a second pin path
                        $difInc = Join-Path $workspaceRoot 'base\libdif\include'
                        if (Test-Path $difInc) {
                            Copy-Item -Recurse (Join-Path $difInc '*') $IncludeOut -Force
                        }
                    }
                [void]$script:results.Add($r)
                $script:pins['libraster'] = $ver
            }
            default {
                Write-Warning "Skipping unsupported lib: $lib"
            }
        }
    }
}

if (-not $SkipVcpkgVendor) {
    $vcpkgRoot = Resolve-VcpkgRootForVendor
    Write-Host "Vendoring from vcpkg: $vcpkgRoot"
    $vendorSpecs = @(
        @{ Name = 'zlib'; Version = '1.3.1'; LibPatterns = @('zlib.lib'; 'zlibstatic.lib'); HeaderGlobs = @('zlib.h', 'zconf.h') },
        @{ Name = 'lzo'; Version = '2.10'; LibPatterns = @('lzo2.lib'); HeaderGlobs = @('lzo') },
        @{ Name = 'snappy'; Version = '1.2.2'; LibPatterns = @('snappy.lib'); HeaderGlobs = @('snappy.h', 'snappy-c.h', 'snappy-sinksource.h', 'snappy-stubs-public.h') },
        @{ Name = 'zstd'; Version = '1.5.7'; LibPatterns = @('zstd.lib'); HeaderGlobs = @('zstd.h', 'zstd_errors.h', 'zdict.h') },
        @{ Name = 'brotli'; Version = '1.1.0'; LibPatterns = @('brotlicommon.lib', 'brotlidec.lib', 'brotlienc.lib'); HeaderGlobs = @('brotli') },
        @{ Name = 'gperftools'; Version = '2.16'; LibPatterns = @('libtcmalloc_minimal.lib', 'tcmalloc_minimal.lib'); HeaderGlobs = @('gperftools') },
        @{ Name = 'gtest'; Version = '1.17.0'; LibPatterns = @('gtest.lib', 'gtest_main.lib'); HeaderGlobs = @('gtest', 'gmock') }
    )
    foreach ($spec in $vendorSpecs) {
        $r = Publish-VcpkgPackage -RepoRoot $repoRoot -VcpkgRoot $vcpkgRoot `
            -Name $spec.Name -Version $spec.Version `
            -LibPatterns $spec.LibPatterns -HeaderGlobs $spec.HeaderGlobs
        [void]$script:results.Add($r)
        $script:pins[$spec.Name] = $spec.Version
    }
}

if ($script:pins.Count -gt 0) {
    Update-VersionsJson -RepoRoot $repoRoot -Pins $script:pins
}

$today = (Get-Date).ToString('yyyy-MM-dd')
$index = @"
---
title: encoder-rle4k-cpu deps
created: $today
author: publish-deps.ps1
last_updated: $today
updated_by: publish-deps.ps1
---

# deps/

Vendored **static libraries + headers + README/VERSION/LICENSE** only.
Pins are in ``versions.json``. After ``publish-deps.ps1``, prefer building the tool
and unit tests against this tree (no live vcpkg when deps are complete).

``deps/`` does **not** ship ``.cpp``/``.c`` or ``src/`` trees. Reviewable base sources
live under workspace ``base/{libdif,libraster,librle4k}``; OSS sources remain
upstream / on the author-machine vcpkg tree used by ``publish-deps.ps1``.

See each package README for origin and link instructions.
"@
Set-Content -Path (Join-Path $repoRoot 'deps\README.md') -Value $index -Encoding utf8

Write-Host ""
Write-Host "=== publish-deps summary ==="
foreach ($r in $script:results) {
    $n = $r['Name']
    $v = $r['Version']
    $p = $r['Path']
    Write-Host ("{0} {1} -> {2}" -f $n, $v, $p)
    if ($r.ContainsKey('Sha256')) {
        Write-Host ("  expiry={0} sha256={1}" -f $r['Expiry'], $r['Sha256'])
    } elseif ($r.ContainsKey('Libs')) {
        Write-Host ("  libs={0}" -f ($r['Libs'] -join ', '))
    }
}
