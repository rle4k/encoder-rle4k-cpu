---
title: Scripts
created: 2026-03-01
author: AI-assisted
last_updated: 2026-08-13
updated_by: AI-assisted
status: active
---


# Scripts

Run from the repository root.

| Script | Purpose |
|--------|---------|
| `msbuild.ps1` | Configure + build `redcli` (Release default) |
| `clean.ps1` | Remove `build/`, `out/` (`-Deep` also `.vs/`, `results/`) |
| `msbuild-common.ps1` | Shared helpers (dot-sourced; do not run alone) |
| `run-unit-tests.ps1` | Build/run Google Test |
| `test-single-file.ps1` | `redcli run` on one `.dif` |
| `test-folder.ps1` | `redcli run --mode 1` on a folder of `*.dif` |
| `run-build-test-file.ps1` | Build then single-file run |
| `publish-deps.ps1` | Author: rebuild/vendor `deps/` |
| `publish-release.ps1` | Author: zip release archive |

```powershell
.\scripts\msbuild.ps1 Release
.\scripts\clean.ps1
.\scripts\clean.ps1 -Deep
.\scripts\test-single-file.ps1 data\DIF50.dif
.\scripts\test-folder.ps1 data
.\scripts\run-unit-tests.ps1
```

Output: `build/redcli.exe` (NMake) or `build/<Config>/redcli.exe` (VS generator).

### Build environment

| Requirement | Detail |
|-------------|--------|
| OS | Windows x64 |
| Toolchain | VS 2022 + MSVC v143 (C++ / Build Tools); VS 2019 fallback if `vcvars64` exists |
| CMake | 3.21+ (prefer VS-bundled) |
| Shell | PowerShell 5.1+ |
| Deps | Complete [`deps/`](../deps/) pins ([`versions.json`](../deps/versions.json)) |

Full table: root [README.md](../README.md) § Build.
