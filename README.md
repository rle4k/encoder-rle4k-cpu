# encoder-rle4k-cpu

Paper reproducibility package for **A1-RLE4K-CPU-P01**: RLE4K CPU bitmap codec
benchmark CLI (`redcli`).

**Name:** `redcli` = **R**aster + **E**ncode + **D**ecode + **CLI** — the
command-line test/benchmark tool for the raster → encode → decode pipeline.

Windows **x64** only.
Source: <https://github.com/rle4k/encoder-rle4k-cpu>
License: [MIT](LICENSE)
(`SPDX: MIT`).
Third-party notices: [NOTICE.md](NOTICE.md).

---

## How it works

```
.dif  →  libdif (geometry)  →  libraster (1-bpp blocks)  →  codecs  →  report/CSV
```

1. **Load** — `libdif` parses a DIF file into `cad_document` (polygons + spatial grids).
2. **Stripe** — the layout is processed as vertical strips of width `strip_width`
   (auto when `0`: `ALIGN(dmd_cols × N + dmd_rows, align_mode)`).
3. **Rasterize** — each strip is split into row blocks of height `block_line`.
   `libraster` produces packed 1-bpp `bitmap_info` blocks (no full-frame image).
4. **Encode / decode** — the same bitmaps are fed to every configured codec.
   Phases are separate: raster → encode → decode. Wall time and CPU time are
   recorded; optional FNV-1a check runs **outside** the decode wall timer.
5. **Report** — per-file / per-strip sizes, ratios, and timings; optional CSV.

**Codecs** (format IDs in `redcli.json` / `--formats`):

| ID | Name   | Implementation                        | Version | Source |
|----|--------|---------------------------------------|---------|--------|
| 0  | RLE4K  | `librle4k` (static lib under `deps/`) | 1.0.0   | <https://github.com/rle4k/encoder-rle4k-cpu> |
| 1  | GZIP   | zlib                                  | 1.3.1   | <https://github.com/madler/zlib> |
| 2  | LZO2   | LZO (`lzo1x`)                         | 2.10    | <https://www.oberhumer.com/opensource/lzo/> |
| 3  | SNAPPY | Snappy                                | 1.2.2   | <https://github.com/google/snappy> |
| 4  | ZSTD   | Zstandard                             | 1.5.7   | <https://github.com/facebook/zstd> |
| 5  | Brotli | Brotli                                | 1.1.0   | <https://github.com/google/brotli> |

Worker threads (`raster_threads` / `encode_threads` / `decode_threads`) are
capped at **10**. Allocator: static **tcmalloc** (`gperftools`).

Layout:

| Path | Role |
|------|------|
| `redcli/` | CLI, multi-codec wrappers, pipeline orchestration |
| `deps/` | Headers + static `.lib` + VERSION/LICENSE (no `.cpp` trees) |
| `cmake/` | Resolves `deps/` pins |
| `data/` | Local DIF inputs (smoke fixture + your corpus) |
| `tests/` | Google Test (`redcli_tests`) |

Reviewable base sources (author workspace, sibling of this repo when present):
`../base/{libdif,libraster,librle4k}`. This package links the published
artifacts under `deps/`.

Base libraries (same **MIT** as this repository):

| Library | Role |
|---------|------|
| `libdif` | DIF → geometry |
| `libraster` | geometry → 1-bpp bitmap |
| `librle4k` | RLE4K encode / decode |

Evaluation builds may time-gate (see each `VERSION`). Contact:
**yuqp78@foxmail.com**.

---

## Why these libraries make a fair, reproducible comparison

The benchmark answers one question: **given the same 1-bpp bitmaps, how do the
codecs differ in size and speed?** The `deps/` tree exists so that question stays
well-defined across machines and over time.

### Fairness (same input, same harness)

| Mechanism | What it prevents |
|-----------|------------------|
| `libdif` / `libraster` stop at `bitmap_info` | Geometry load or rasterization cannot favor RLE4K inside encode/compare |
| One raster path → all codecs | No per-codec preprocessing, padding, or different bitmaps |
| Phased timing: raster → encode → decode | Raster cost is not mixed into codec wall time; optional FNV check runs **outside** decode wall |
| Shared `redcli` + config defaults | Same strip/block geometry, thread caps (≤10), levels, and `repeat` for every codec |
| Shared static **tcmalloc** | Allocator differences do not masquerade as codec differences |

In short: **input identity** (identical bitmaps) + **measurement identity** (same
CLI, phases, allocator, parameters). Codecs compete only on encode/decode of
that shared payload.

### Reproducibility (pinned artifacts, not floating builds)

| Mechanism | What it guarantees |
|-----------|-------------------|
| `deps/versions.json` pins | Reviewers link the same library versions the paper used |
| Headers + static `.lib` under `deps/` (no `.cpp` trees) | Day-to-day rebuilds do not silently pull a different algorithm from a live vcpkg tree |
| Public Gerber → DIF corpus + fixed experiment knobs | Same layouts and raster settings as the manuscript |
| Open `redcli/` orchestration | Timing model and report format are inspectable |

Shipping prebuilt base libs under `deps/` is intentional: the comparison surface
is the **codec on bitmaps**, not a re-derivation of proprietary CAM tooling.
Anyone with this tree can rebuild `redcli`, point it at the same `.dif` files,
and obtain comparable CS / CR / encode–decode timings under the documented
defaults.

---

## Data

Input: **`.dif` only** (no Gerber / GDS in this tree).

| Item | Location / source |
|------|-------------------|
| Smoke fixture | `data/DIF50.dif` |
| Paper corpus | **64** Gerber RS-274X samples → convert to `DIF01.dif` … `DIF64.dif` |

Public Gerber set (same as manuscript Code and Data Availability):

| Source | URL |
|--------|-----|
| IEEE DataPort | https://dx.doi.org/10.21227/t0cy-rb10 |
| GitHub | https://github.com/disking-cn/gerber_sample_files |
| Gitee | https://gitee.com/disking-cn/gerber_sample_files |

Convert Gerber → DIF with external **`gbrcli`** (not shipped here). Place `.dif`
files under `data/` or pass any path to `--input`. Details: [data/README.md](data/README.md),
[samples/README.md](samples/README.md).

---

## Experiment method

1. `redcli init` → edit `redcli.json` (or pass CLI flags).
2. Point `input` at `.dif` files; keep geometry / codec levels at paper defaults
   for comparable tables.
3. `redcli run` → console Comparison Report; optional `--csv`.

**Operator field guide (every key, range, effect):**  
[docs/redcli.help.md](docs/redcli.help.md) · annotated defaults
[docs/redcli.example.json](docs/redcli.example.json).

Paper defaults (auto strip → **61952** px):

| Key | Paper | Notes |
|-----|------:|-------|
| `pitch_width_um` | `0.5` | µm/pixel; changes bitmap size |
| `oblique_factor` | `32` | N in auto strip width |
| `dmd_resolution_*` | `512`×`1920` | Auto strip only |
| `strip_width` | `0` | `0` = `ceil((cols×N+rows)/align)×align` |
| `align_mode` | `512` | Auto-width rounding grain |
| `block_line` | `512` | Row-block height (px) |
| `repeat` | `3` | Averaged timing repeats |
| `formats` | `[0..5]` | All six codecs |
| `gzip_level` / `zstd_level` / `brotli_level` | `6` / `3` / `6` | Third-party levels |
| `lzo2_level` | `1` | Fast LZO path |
| `check_enabled` | `0` | `1` = FNV verify (outside decode wall) |
| `raster_threads` | `4` | Encode/decode `0` → same; cap **10** |

```powershell
.\build.bat
.\build\Release\redcli.exe init
.\build\Release\redcli.exe edit          # set input, tune fields
.\build\Release\redcli.exe run
# or one-shot:
.\build\Release\redcli.exe run --input data\DIF50.dif
.\scripts\test-folder.ps1 data
```

---

## Libraries and versions

Pins: [`deps/versions.json`](deps/versions.json).

| Package | Version | Role | Source |
|---------|---------|------|--------|
| libdif | 1.0.0 | DIF → geometry | <https://github.com/rle4k/encoder-rle4k-cpu> |
| libraster | 1.0.0 | geometry → 1-bpp bitmap | <https://github.com/rle4k/encoder-rle4k-cpu> |
| librle4k | 1.0.0 | RLE4K encode/decode | <https://github.com/rle4k/encoder-rle4k-cpu> |
| zlib | 1.3.1 | GZIP | <https://github.com/madler/zlib> |
| lzo | 2.10 | LZO2 | <https://www.oberhumer.com/opensource/lzo/> |
| snappy | 1.2.2 | SNAPPY | <https://github.com/google/snappy> |
| zstd | 1.5.7 | ZSTD | <https://github.com/facebook/zstd> |
| brotli | 1.1.0 | Brotli | <https://github.com/google/brotli> |
| gperftools | 2.16 | tcmalloc | <https://github.com/gperftools/gperftools> |
| gtest | 1.17.0 | unit tests | <https://github.com/google/googletest> |

Licenses and link notes: [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
Override pin: env `RLE4K_DEPS_<NAME>_VERSION` or CMake `-DRLE4K_LIBDIF_VERSION=…`
(and peers) / `-DRLE4K_DEPS_ROOT=…`.

Day-to-day builds use **`deps/` only** (no live vcpkg required when the tree is
complete). Author harvest into `deps/`: `.\scripts\publish-deps.ps1`.

---

## Build

### Build environment

| Requirement | Detail |
|-------------|--------|
| OS | **Windows x64** only (no Linux / macOS / Win32) |
| Toolchain | **Visual Studio 2022** (v17) with **Desktop development with C++**, or **Build Tools for VS 2022**, including MSVC **v143** (`VC.Tools.x86.x64`) |
| Fallback | VS 2019 may work if `vcvars64.bat` is present; scripts prefer VS 2022 / v143 |
| Language / runtime | **C++17**, MSVC **`/MT`** static CRT (matches vendored static libs) |
| CMake | **3.21+** (VS 2022 bundled CMake is fine). Project `cmake_minimum_required` is 3.16; scripts warn below 3.21 |
| Shell | **PowerShell 5.1+** (`build.bat` → `scripts\msbuild.ps1`) |
| Vendored deps | Complete **`deps/`** tree (`deps/versions.json` pins). Day-to-day builds link **`deps/` only** — no live vcpkg when that tree is complete |
| vcpkg (optional) | Needed only if `deps/` is incomplete, or for author harvest / tcmalloc install. Triplet: **`x64-windows-static`**. Detect order: `VCPKG_ROOT` → well-known roots (`D:\vcpkg`, `%USERPROFILE%\vcpkg`, `C:\vcpkg`, `E:\vcpkg`) → `vcpkg root` (VS-bundled `VC\vcpkg` is rejected) |

```powershell
.\build.bat
# Debug: .\scripts\msbuild.ps1 Debug
.\clean.bat                    # or .\scripts\clean.ps1
.\scripts\clean.ps1 -Deep      # also .vs/, results/
```

`build.bat` / `scripts/msbuild.ps1` always wipe `build/` and any leftover VS
Open-Folder tree `out/` before reconfigure (those caches store **absolute**
source paths and break after you move the repo). Use `clean.bat` /
`scripts/clean.ps1` to remove those without rebuilding (`-Deep` also drops
`.vs/` and legacy `results/`). Does not touch `deps/`, `data/`, or sources.

**After relocating the tree:** close VS 2022, run `.\scripts\clean.ps1 -Deep`,
then `.\build.bat`. Prefer the CLI build for reproducibility; if you open the
folder in VS again, let it regenerate CMake under a fresh `out/`.

| Artifact | Path |
|----------|------|
| CLI | `build\redcli.exe` or `build\Release\redcli.exe` |
| Unit tests | `.\scripts\run-unit-tests.ps1` |

**Remote rebuild (GitHub / Gitee):** commit `deps/` (headers + `.lib`), `cmake/`,
and `data/README.md` + `data/DIF50.dif` with the sources. Without `deps/`, the
public tree cannot link.

---

## Citation

Cite paper **A1-RLE4K-CPU-P01** when using this package for academic work.
