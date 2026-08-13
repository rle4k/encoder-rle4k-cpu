# Contributing

This repository is the open reproducibility package for the RLE4K CPU codec
benchmark (`redcli`), paper **A1-RLE4K-CPU-P01**.

## In scope

- Bug fixes in `redcli/` and build scripts
- Documentation corrections
- Reproducibility improvements (build, config, benchmark output)

## Out of scope

- Production CAD parsers (`.gbr`, `.gds`, …)
- Commercial CAM / maskless pipeline code

## Setup

Build environment (see root [README.md](README.md) § Build):

| Requirement | Detail |
|-------------|--------|
| OS | Windows **x64** only |
| Toolchain | VS **2022** + MSVC **v143** (C++ workload or Build Tools) |
| CMake | **3.21+** (VS-bundled CMake OK) |
| Shell | PowerShell **5.1+** |
| Deps | Complete **`deps/`** (`deps/versions.json`); no live vcpkg when vendored |

```powershell
.\scripts\msbuild.ps1 Release
.\scripts\run-unit-tests.ps1
```

`redcli init` writes `redcli.json` from built-in defaults
(`rle4k_builtin_default_config_json` in `redcli/rle4k_config.h`).
Keep [docs/redcli.example.json](docs/redcli.example.json) aligned when defaults change.
Do not commit machine-specific `redcli.json`.

## Pull requests

1. Keep changes focused; match existing `rle4k_*` / snake_case naming.
2. Verify `.\scripts\msbuild.ps1 Release`.
3. Update docs when behavior or paths change.

## License

Contributions are accepted under the project [LICENSE](LICENSE)
([MIT License](https://opensource.org/licenses/MIT), `SPDX: MIT`),
including the base libraries under `deps/`.
Third-party upstream components keep their own licenses ([NOTICE.md](NOTICE.md)).
