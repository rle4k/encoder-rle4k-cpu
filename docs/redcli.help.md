---
title: redcli Usage Guide
created: 2026-07-24
author: AI-assisted
last_updated: 2026-08-13
updated_by: AI-assisted
status: active
---

# redcli — Usage Guide

**Name:** `redcli` = **R**aster + **E**ncode + **D**ecode + **CLI** — the
command-line test/benchmark tool for the raster → encode → decode pipeline.

`redcli` benchmarks codecs on 1-bpp bitmaps produced from `.dif` geometry:
load → strip rasterize → encode/decode → console report (optional CSV).

Input is **`.dif` only**. Pipeline / fairness: [../README.md](../README.md).
Annotated defaults: [redcli.example.json](redcli.example.json) (must stay aligned with
`rle4k_builtin_default_config_json` in `redcli/rle4k_config.h`).

---

## Quick start

```powershell
.\scripts\msbuild.ps1 Release
.\build\Release\redcli.exe init          # write redcli.json (skip if exists)
.\build\Release\redcli.exe edit          # open in default editor
.\build\Release\redcli.exe run --input data\DIF50.dif
```

Exe: `build/redcli.exe` (NMake) or `build/Release/redcli.exe` (VS). CLI flags
override matching keys in `redcli.json`.

| Command | Action |
|---------|--------|
| `init` | Create `redcli.json` from built-in defaults (never overwrite) |
| `run` / `benchmark` | Run benchmark |
| `edit` / `config` | Open resolved `redcli.json` |
| `-h` / `--help` | Help |

**Search order for `redcli.json`:** `.` → `..` → exe dir → exe `../` / `../../` → `docs` / `../docs`.
If missing: warn + built-in defaults; **`input` / `--input` still required**.

---

## Geometry vocabulary

```text
DIF (vector)
  └─ strip columns of width strip_width
       └─ row blocks of height block_line  →  one bitmap_info each
            └─ sliding window of cache_blocks_max blocks  →  raster → encode → decode
```

There is **no full-frame pixel image**. Changing `pitch_width_um`,
`strip_width` / auto formula, or `block_line` changes the bitmaps every codec sees
— keep them fixed for paper-comparable runs.

---

## Configuration fields (operator reference)

JSON key names below are what you edit in `redcli.json`. Types: string / int /
double / int array. Unknown keys are ignored. `//` comments are allowed.

### Input and output

#### `input` (string, default `""`)

Path to a single `.dif` (`mode=0`) or a directory of `*.dif` (`mode=1`).
Relative paths are relative to the **process working directory**, not the config file.

- Set this first. Empty → `[ERROR] --input required`.
- CLI: `--input` / `-i`.

#### `mode` (int, default `0`)

| Value | Behavior |
|------:|----------|
| `0` | One file: `input` must be a `.dif` path |
| `1` | Folder: non-recursive scan of `input` for `*.dif` (extension exactly `.dif`) |

CLI: `--mode` / `-m`.

#### `output_path` (string, default `"build/out"`)

Conventional directory for logs / future artifacts. Created if missing.
**CSV is not written unless you pass `--csv` / `-o`** with an explicit file path.
Changing `output_path` alone does not create a comparison CSV.

---

### Timing and integrity

#### `repeat` (int, default `3`)

How many times encode/decode timing is repeated per measurement cell; results are
averaged. Higher → more stable timings, longer wall clock. Paper default: `3`.

CLI: `--repeat` / `-r`.

#### `check_enabled` (int, default `0`)

| Value | Behavior |
|------:|----------|
| `0` | No integrity check (default; fastest; CHK=`SKIP`) |
| `1` | After raster: FNV-1a digest of each raw block. After decode: recompute FNV and compare. Verify runs **outside** the decode wall timer so it does not inflate decode GB/s |

Use `1` when validating a build; keep `0` for paper timing tables unless the
manuscript says otherwise.

CLI: `--check` / `--no-check`.

---

### Raster geometry (affects bitmaps — change carefully)

#### `pitch_width_um` (double, default `0.5`)

Pixel pitch in **micrometres**. Converted to nm as `pw_nm = pitch_width_um * 1000`
for rasterization. Smaller pitch → larger pixel grid for the same physical layout
→ more data, longer runs.

Paper value: `0.5`. CLI: `--pw`.

#### `oblique_factor` (int, default `32`)

Oblique factor **N**. Used only when `strip_width == 0` (auto width):

```text
raw = dmd_resolution_cols × oblique_factor + dmd_resolution_rows
strip_width = ceil(raw / align_mode) × align_mode
```

With defaults: `1920×32 + 512 = 61952` → already divisible by `512` → strip width
**61952** px. Larger N → wider strips → fewer columns, larger blocks.

Paper value: `32`. CLI: `-N` / `--oblique`.

#### `dmd_resolution_rows` / `dmd_resolution_cols` (int, defaults `512` / `1920`)

Nominal DMD size used **only** in the auto `strip_width` formula above.
They do not change the DIF document bounds; they model the exposure aperture
geometry used to size strips. Keep paper defaults unless you intentionally change
the strip model.

#### `strip_width` (int, default `0`)

Strip width in **pixels**.

| Value | Meaning |
|------:|---------|
| `0` | Auto (formula above) — preferred for paper runs |
| `>0` | Force this width; ignore DMD / N / align for sizing |

Larger width → fewer strip columns, more pixels per block row. Must be ≥ 1 after
auto resolution.

CLI: `--strip-width` / `-sw`.

#### `align_mode` (int, default `512`)

Rounding grain for **auto** `strip_width` (same units as width in pixels in the
ceil formula). Common values: `8`, `32`, `64`, `512`. Paper: `512`.

Note: the rasterizer path currently packs blocks with a fixed **64-bit** row
align (`ALIGN064`) inside the encode wrappers; `align_mode` here is for strip
width rounding, not a free choice of bitmap stride for codecs.

#### `block_line` (int, default `512`)

Row-block height in **pixel rows**. One raster/encode/decode unit is a block of
size roughly `strip_width × block_line` (1 bpp). Smaller → more blocks, finer
windowing, more thread work items; larger → heavier per-block buffers.

Paper: `512`. CLI: `--strip-height` / `-sh`.

---

### Sliding window (memory vs parallelism)

#### `cache_blocks_min` (int, default `32`)

Low watermark for the sliding raw-block window (documentation / backpressure
semantics). Clamped to ≥ 1. If `cache_blocks_max < min`, max is raised to min.

CLI: `--cache-blocks-min`.

#### `cache_blocks_max` (int, default `128`)

**Window size**: how many consecutive row-blocks are rasterized, then encoded,
then decoded together before advancing. Larger → more RAM (holds up to `max` raw
and encoded blocks), fewer phase transitions. Smaller → less RAM, more phase
churn.

Effective window = `max(min, max)`. CLI: `--cache-blocks-max` or `--cache-blocks`
(alias for max).

---

### Threading and phase switches

Hard cap: **all** of `raster_threads` / `encode_threads` / `decode_threads` are
clamped to **1…10**. Env **`RLE4K_THREADS`** (positive int) overrides
`raster_threads` after config/CLI parse.

#### `raster_threads` (int, default `4`)

Phase-1 workers: rasterize blocks inside the current window. CLI: `--threads` /
`--raster-threads` / `-t`.

#### `encode_threads` (int, default `0`)

Phase-2 workers. `0` → use `raster_threads`. CLI: `--encode-threads` / `-et`.

#### `decode_threads` (int, default `0`)

Phase-3 workers. `0` → use `raster_threads`. CLI: `--decode-threads` / `-dt`.

Tips:

- Raise threads toward CPU count (≤10) for faster wall time on large files.
- For apples-to-apples paper numbers, keep the same thread settings across machines
  when possible, or report them with the results.

#### `encode_enabled` / `decode_enabled` (int, default `1` / `1`)

| Field | `1` | `0` |
|-------|----|----|
| `encode_enabled` | Run encode | Skip encode (and decode is skipped too) |
| `decode_enabled` | Run decode | Skip decode only |

CLI: `--no-encode` / `--no-decode`. Use `--no-encode` for raster-only smoke.

---

### Codecs

#### `formats` (int array, default `[0,1,2,3,4,5]`)

Ordered list of config format IDs to run. Unknown IDs are skipped. Empty after
filtering → all six codecs.

| ID | Codec | Library |
|---:|-------|---------|
| `0` | RLE4K | `librle4k` |
| `1` | GZIP | zlib (`compress2`) |
| `2` | LZO2 | LZO |
| `3` | SNAPPY | Snappy |
| `4` | ZSTD | Zstandard |
| `5` | Brotli | Brotli |

Examples: `"[0]"` RLE4K only; `"[0,4]"` RLE4K vs ZSTD. Order is run order.
**Per-format re-raster is intentional** so each codec’s timing starts from a
fresh raster of the same geometry (fair size/speed compare, not shared warm cache
across codecs).

#### `gzip_level` (int, default `6`)

zlib level **1…9** (clamped). Higher → better ratio, slower. Paper: `6`.

#### `zstd_level` (int, default `3`)

Zstd level **1…22** (clamped). Paper: `3`.

#### `lzo2_level` (int, default `1`)

| Value | API |
|------:|-----|
| `1` | `lzo1x_1_compress` (fast default) |
| `2`…`999` | `lzo1x_999_compress_level` |

Paper: `1`.

#### `snappy_level` (int, default `0`)

Reserved; Snappy has no compression level. Ignored.

#### `brotli_level` (int, default `6`)

Brotli quality **0…11** (clamped). Paper: `6`.

#### `brotli_window` (int, default `22`)

Brotli window bits **10…24** (clamped). Paper: `22` (Brotli default window).

---

### Logging

#### `verbose` (string, default `"file"`)

| Value | Stderr detail |
|-------|----------------|
| `"file"` | Per-file progress / summaries |
| `"strip"` | Extra per-strip / per-column timing detail |

CLI: `--verbose` / `-v`.

---

## `run` CLI cheat sheet

```text
redcli run [options]
```

| Option | Config key |
|--------|------------|
| `-i` / `--input` | `input` |
| `-m` / `--mode` | `mode` |
| `-r` / `--repeat` | `repeat` |
| `--pw` | `pitch_width_um` |
| `-N` / `--oblique` | `oblique_factor` |
| `-sh` / `--strip-height` | `block_line` |
| `-sw` / `--strip-width` | `strip_width` |
| `-t` / `--threads` / `--raster-threads` | `raster_threads` |
| `-et` / `--encode-threads` | `encode_threads` |
| `-dt` / `--decode-threads` | `decode_threads` |
| `--cache-blocks-min` / `--cache-blocks-max` / `--cache-blocks` | cache watermarks |
| `--check` / `--no-check` | `check_enabled` |
| `--no-encode` / `--no-decode` | phase switches |
| `-v` / `--verbose` | `verbose` |
| `-o` / `--csv` / `--output` | CSV file path (not `output_path`) |
| `--force` | accepted, unused (no resume) |

---

## Pipeline and timing

```text
DIF load
  → for each format (re-raster intentional):
       for each strip column:
         for each row window (size = cache_blocks_max):
           Phase 1  raster   (wall; optional FNV if check)
           Phase 2  encode   (wall; drop raw after encode)
           Phase 3  decode   (wall; FNV verify OUTSIDE wall if check)
  → Comparison Report (stdout)
  → optional --csv
```

Throughput uses phase **wall-clock** (thread-pool spawn→join).

---

## Output

**Console:** per-file Comparison Report — CS, CR, encode/decode time & throughput, CHK (`OK` / `FAIL` / `SKIP`). Progress on stderr.

**CSV** (`--csv path`): UTF-8 with BOM; one row per DIF; columns like `RLE4K_cr`,
`RLE4K_cs_mb`, `RLE4K_sed_ms`, `RLE4K_set_gbps`, `RLE4K_sdd_ms`, `RLE4K_sdt_gbps`,
`RLE4K_chk`.

---

## Operator recipes

```powershell
# Paper-like single file
.\build\Release\redcli.exe run -i data\DIF50.dif -r 3

# Folder corpus + CSV
.\build\Release\redcli.exe run -i data --mode 1 --csv build\out\compare.csv

# Raster only
.\build\Release\redcli.exe run -i data\DIF50.dif --no-encode

# RLE4K vs ZSTD only, integrity on
# (edit formats / check_enabled in redcli.json, or run all and ignore columns)

# More threads, strip detail
.\build\Release\redcli.exe run -i data\DIF50.dif -t 8 -et 8 -dt 8 -v strip
```

Edit defaults once:

```powershell
.\build\Release\redcli.exe init
.\build\Release\redcli.exe edit
# set "input", adjust fields, save, then:
.\build\Release\redcli.exe run
```

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `--input required` | Set `input` or pass `-i` |
| `No .dif files` | Check path; `mode=1` needs `*.dif` in the folder |
| `redcli.json not found` | `redcli init` |
| Timings not comparable to paper | Restore pitch / N / block_line / strip_width=0 / align / DMD / formats / levels / threads |
| OOM on large layouts | Lower `cache_blocks_max` or `block_line` / `strip_width` |
| Editor does not open | Set `EDITOR` / `VISUAL` |

---

## Related

| Doc | Contents |
|-----|----------|
| [redcli.example.json](redcli.example.json) | Annotated default config |
| [DEPENDENCIES.md](DEPENDENCIES.md) | Library pins |
| Root [README.md](../README.md) | Fairness, data, build |
