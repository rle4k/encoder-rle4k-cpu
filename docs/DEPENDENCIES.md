---
title: Dependencies
created: 2026-06-28
author: AI-assisted
last_updated: 2026-08-13
updated_by: AI-assisted
status: active
---

# Dependencies

Pins: [`deps/versions.json`](../deps/versions.json). Notices: [NOTICE.md](../NOTICE.md).
Build: [README.md](../README.md).

This repository (including `redcli/` and base libraries **libdif** /
**libraster** / **librle4k**) is [MIT](../LICENSE). Other vendored
third-party artifacts under `deps/` keep their upstream licenses.

All runtime/link deps live under [`deps/`](../deps/) as **headers + static `.lib` +
README/VERSION/LICENSE**. No `.cpp`/`.c` trees. Reviewable base sources (when
present): workspace `base/{libdif,libraster,librle4k}`.

## Summary

| Library | Version | Used by | License | Source |
|---------|---------|---------|---------|--------|
| libdif | 1.0.0 | DIF → geometry | MIT | <https://github.com/rle4k/encoder-rle4k-cpu> |
| libraster | 1.0.0 | geometry → bitmap | MIT | <https://github.com/rle4k/encoder-rle4k-cpu> |
| librle4k | 1.0.0 | RLE4K encode/decode | MIT | <https://github.com/rle4k/encoder-rle4k-cpu> |
| zlib | 1.3.1 | GZIP | zlib | <https://github.com/madler/zlib> |
| LZO | 2.10 | LZO2 | GPL-2.0 | <https://www.oberhumer.com/opensource/lzo/> |
| Snappy | 1.2.2 | SNAPPY | BSD-3-Clause | <https://github.com/google/snappy> |
| Zstandard | 1.5.7 | ZSTD | BSD / GPL-2.0 | <https://github.com/facebook/zstd> |
| Brotli | 1.1.0 | Brotli | MIT | <https://github.com/google/brotli> |
| gperftools | 2.16 | tcmalloc (`redcli`) | BSD-3-Clause | <https://github.com/gperftools/gperftools> |
| gtest | 1.17.0 | `redcli_tests` | BSD-3-Clause | <https://github.com/google/googletest> |

Base libs share **MIT** with this repository.
`libdif` / `libraster` stop at bitmaps (no encode/compare). Compression libs and
tcmalloc link **statically** (`/MT`). Contact: **yuqp78@foxmail.com**.

## Link map

| Format ID | Name | Library artifact | Version | Source |
|-----------|------|------------------|---------|--------|
| 0 | RLE4K | `deps/librle4k/<ver>/lib/…` | 1.0.0 | <https://github.com/rle4k/encoder-rle4k-cpu> |
| 1 | GZIP | `deps/zlib/<ver>/lib/zlib.lib` | 1.3.1 | <https://github.com/madler/zlib> |
| 2 | LZO2 | `deps/lzo/<ver>/lib/lzo2.lib` | 2.10 | <https://www.oberhumer.com/opensource/lzo/> |
| 3 | SNAPPY | `deps/snappy/<ver>/lib/snappy.lib` | 1.2.2 | <https://github.com/google/snappy> |
| 4 | ZSTD | `deps/zstd/<ver>/lib/zstd.lib` | 1.5.7 | <https://github.com/facebook/zstd> |
| 5 | Brotli | `deps/brotli/<ver>/lib/…` | 1.1.0 | <https://github.com/google/brotli> |

tcmalloc: `deps/gperftools/<ver>/lib/libtcmalloc_minimal.lib` with
`USE_TCMALLOC`, `PERFTOOLS_DLL_DECL=`, and MSVC
`/INCLUDE:??0TCMallocGuard@@QEAA@XZ`.

CMake: `cmake/DepsLibdif.cmake`, `DepsLibraster.cmake`, `DepsLibrle4k.cmake`,
`DepsVendoredCompression.cmake`.

## Author harvest

```powershell
.\scripts\publish-deps.ps1
# optional: -ExpiryDate YYYY-MM-DD  -LibVersion 1.0.0  -SkipVcpkgVendor  -SkipBase
```

Rebuilds base libs (evaluation gate unless `-NoGate`), vendors compression /
gtest / tcmalloc into `deps/`, updates `versions.json`. Day-to-day engineer
builds do not need live vcpkg when `deps/` is complete.
