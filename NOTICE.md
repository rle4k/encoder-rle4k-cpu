# Notice

**encoder-rle4k-cpu** (including `redcli/`, scripts, docs authored here,
packaging, and the base libraries **libdif**, **libraster**, **librle4k**)
is licensed under the [MIT License](LICENSE)
(`SPDX-License-Identifier: MIT`).

Copyright 2026

| Library | Role | License |
|---------|------|---------|
| libdif | DIF → geometry | MIT (`deps/libdif/<ver>/LICENSE`) |
| libraster | geometry → bitmap | MIT (`deps/libraster/<ver>/LICENSE`) |
| librle4k | RLE4K encode/decode | MIT (`deps/librle4k/<ver>/LICENSE`) |

Evaluation builds of the base libraries may embed a time-limited gate
(see each `deps/<name>/<ver>/VERSION`). Contact: **yuqp78@foxmail.com**.

## Third-party components

Vendored under [`deps/`](deps/) (pins: [`deps/versions.json`](deps/versions.json)).
Summary: [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
Upstream licenses of third-party libraries remain as listed below.

| Component | Role | License |
|-----------|------|---------|
| zlib | GZIP / DEFLATE | zlib License |
| LZO (lzo2) | LZO2 | GPL-2.0 |
| Snappy | SNAPPY | BSD-3-Clause |
| Zstandard (zstd) | ZSTD | BSD / GPL-2.0 (dual) |
| Brotli | Brotli | MIT |
| gperftools (`libtcmalloc_minimal`) | Allocator | BSD-3-Clause |
| GoogleTest | Unit tests | BSD-3-Clause |

## Input data

Benchmark runs consume **DIF** files produced as:

1. Download sample Gerber data from **any** of these sources:
   - https://dx.doi.org/10.21227/t0cy-rb10
   - https://github.com/disking-cn/gerber_sample_files
   - https://gitee.com/disking-cn/gerber_sample_files
2. Convert Gerber → DIF with **gbrcli** (not bundled in this repository).
