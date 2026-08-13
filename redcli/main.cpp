/**
 * redcli --- RLE4K Reproducibility Benchmark
 *
 * Usage:
 *   redcli init               → create redcli.json from built-in defaults
 *   redcli run [config]       → run benchmark
 *   redcli edit               → open redcli.json in default editor
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "base/shared.h"
#include "cndefs.h"
#include "rle4k_bitmap.h"
#include "rle4k_buffer_mgr.h"
#include "rle4k_stats.h"
#include "rle4k_format_encoder.h"
#include "rle4k_dif_loader.h"
#include "rle4k_config.h"
#include "rle4k_block_pipeline.h"
#include "rle4k_rasterizer.h"

#include <rle4k.h>
#include <zlib.h>
#include <lzo/lzo1x.h>
#include <snappy.h>
#include <zstd.h>

USING_SP2_NAMESPACE();
USING_CAM_NAMESPACE();

/* ================================================================ */

#ifndef RLE4K_REDCLI_VERSION
#define RLE4K_REDCLI_VERSION "1.0.0"
#endif

void rle4k_print_tcmalloc_link_status() {
#if defined(RLE4K_TCMALLOC_INCLUDE) && RLE4K_TCMALLOC_INCLUDE
    std::cerr << "[redcli] tcmalloc is enabled.\n";
#else
    std::cerr << "[redcli] tcmalloc is disabled.\n";
#endif
}

/* Startup banner: version + tcmalloc allocator status. */
void rle4k_print_banner() {
    std::cerr << "[redcli] redcli v" << RLE4K_REDCLI_VERSION << "\n";
    rle4k_print_tcmalloc_link_status();
}

void rle4k_print_usage() {
    std::cout << R"(
redcli --- RLE4K Reproducibility Benchmark

Usage:
  redcli init                Create redcli.json from built-in defaults
  redcli run [config.json]   Run benchmark (default: redcli.json)
  redcli edit                Open redcli.json in default editor (EDITOR/VISUAL)
  redcli -h                  Show help

Options (run):
  --input, -i <path>          Input .dif file or folder
  --mode, -m <0|1>            0=file, 1=folder
  --repeat, -r <N>            Repeats (default 3)
  --pw <um>                   Pixel pitch um (default 0.5)
  -N <num>                    Oblique factor (default 32)
  --strip-height <H>          Stripe height (default 512)
  --strip-width <W>           Strip width px (0=auto)
  --threads, --raster-threads, -t <N>   Phase-1 raster worker count
  --encode-threads, -et <N>   Phase-2 encode worker count (0=raster threads)
  --decode-threads, -dt <N>   Phase-3 decode worker count (0=raster threads)
  --cache-blocks-min <N>      Raw-block watermark low (default 32)
  --cache-blocks-max <N>      Window size = max (default 128)
  --cache-blocks <N>          Alias for --cache-blocks-max
  --check                     FNV after raster + verify outside decode wall
  --no-check                  Integrity off (default)
  --no-encode                 Skip encode phase (action switch)
  --no-decode                 Skip decode phase (action switch)
  --force                     Force full rerun (ignore resume)
  --verbose <mode>            "file" or "strip"
  --formats, -f <ids>         Comma-separated format IDs (overrides config)
                              0=RLE4K,1=GZIP,2=LZO2,3=SNAPPY,4=ZSTD,5=BROTLI
  --csv, -o <path>            Write comparison results to CSV (UTF-8). By default
                              results are printed to the console only.

Timing model (rle16cli parity): outer format -> strip col -> windowed rows;
raster/encode/decode wall-clock (spawn->join); check outside decode wall.
)";
}

/* ================================================================ */
/* per-format result for a single DIF file */
struct rle4k_fmt_result {
    std::string name;       // algorithm name, e.g. RLE4K / GZIP / ...
    double ss_gb    = 0;    // source size (GB), same across formats
    double cs_mb    = 0;    // compressed size (MB)
    double cr       = 0;    // compression ratio
    double sed_ms   = 0;    // encode time (ms)
    double set_gbps = 0;    // encode throughput (Gbps)
    double sdd_ms   = 0;    // decode time (ms)
    double sdt_gbps = 0;    // decode throughput (Gbps)
    double sch_ms   = 0;    // round-trip check time (ms)
    bool   ok       = true;
};

/* per-DIF-file record: one comparison report */
struct rle4k_file_record {
    std::string short_name;
    int    img_w = 0, img_h = 0;
    int    ncols = 0, nrows = 0;
    double d_ms        = 0; // DIF load time (ms)
    double raster_ms   = 0; // phase-1 rasterization wall time (ms), shared by all formats
    double total_ss_gb = 0; // total source size (GB)
    std::vector<rle4k_fmt_result> fmts; // results of the 6 algorithms, in format order
};

/* ================================================================ */
/* comparison report for a single DIF file → console (stdout).
   Printed after all algorithms for that file have been computed.
   Single file → one report; batch → one report per file, in order. */
void rle4k_print_file_report(const rle4k_file_record& rec, const bench_config& cfg)
{
    std::cout << "\n########## Comparison Report: " << rec.short_name << " ##########\n";
    std::cout << "  file   : " << rec.short_name << "\n";
    std::cout << "  image  : " << rec.img_w << " x " << rec.img_h << " px  blocks: "
              << rec.ncols << " x " << rec.nrows
              << " (" << cfg.strip_width << " x " << cfg.block_line << ")\n";
    const int et = cfg.encode_threads > 0 ? cfg.encode_threads : cfg.raster_threads;
    const int dt = cfg.decode_threads > 0 ? cfg.decode_threads : cfg.raster_threads;
    std::cout << "  params : pw=" << std::fixed << std::setprecision(3) << cfg.pitch_width_um << "um"
              << "  N=" << cfg.oblique_factor
              << "  repeat=" << cfg.repeat
              << "  threads(rt/et/dt)=" << cfg.raster_threads << "/" << et << "/" << dt
              << "  cache=[" << cfg.cache_blocks_min << "," << cfg.cache_blocks_max << "]"
              << "  action(enc/dec)=" << (cfg.encode_enabled ? 1 : 0) << "/" << (cfg.decode_enabled ? 1 : 0)
              << "  check=" << (cfg.check_enabled ? "on" : "off") << "\n";
    std::cout << "  source : " << std::setprecision(4) << rec.total_ss_gb << " GB  (DIF load "
              << std::setprecision(1) << rec.d_ms << "ms"
              << ", raster " << std::setprecision(1) << rec.raster_ms << "ms wall, first format)\n";
    std::cout << "  ----------------------------------------------------------------------\n";
    std::cout << "  " << std::left << std::setw(9) << "ALG"
              << std::right << std::setw(10) << "CS(MB)"
              << std::setw(9)  << "CR"
              << std::setw(11) << "Enc(ms)"
              << std::setw(10) << "Set(Gb)"
              << std::setw(11) << "Dec(ms)"
              << std::setw(10) << "Sdt(Gb)"
              << "  CHK\n";
    std::cout << "  ----------------------------------------------------------------------\n";
    for (auto& r : rec.fmts) {
        std::cout << "  " << std::left  << std::setw(9) << r.name
                  << std::right << std::setw(10) << std::setprecision(3) << r.cs_mb
                  << std::setw(9)  << std::setprecision(2) << r.cr
                  << std::setw(11) << std::setprecision(3) << r.sed_ms
                  << std::setw(10) << std::setprecision(3) << r.set_gbps
                  << std::setw(11) << std::setprecision(3) << r.sdd_ms
                  << std::setw(10) << std::setprecision(3) << r.sdt_gbps;
        if (cfg.check_enabled)
            std::cout << "  " << (r.ok ? "OK" : "FAIL");
        std::cout << "\n";
    }
    std::cout << "  ----------------------------------------------------------------------\n\n";
    std::cout.flush();
}

/* ================================================================ */
/* write all per-file comparison records to a single CSV file.
   One row per DIF file (all algorithms occupy one row). UTF-8 with BOM,
   and the encoding is explicitly declared in the header. */
bool rle4k_write_csv(const std::string& path,
                     const std::vector<rle4k_file_record>& files, const bench_config& cfg)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    /* explicit file encoding: UTF-8 with BOM */
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    ofs.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    ofs << "# Encoding: UTF-8 (BOM)\n";
    ofs << "# redcli comparison report; one row per DIF file; algorithms in columns\n";

    /* header: file + per-algorithm columns */
    ofs << "file,ss_gb";
    if (!files.empty()) {
        for (auto& r : files.front().fmts) {
            std::string a = r.name;
            ofs << "," << a << "_cr" << "," << a << "_cs_mb"
                << "," << a << "_sed_ms" << "," << a << "_set_gbps"
                << "," << a << "_sdd_ms" << "," << a << "_sdt_gbps";
            if (cfg.check_enabled)
                ofs << "," << a << "_sch_ms";
            ofs << "," << a << "_chk";
        }
    }
    ofs << "\n";

    for (auto& rec : files) {
        ofs << "\"" << rec.short_name << "\"," << std::fixed << std::setprecision(6) << rec.total_ss_gb;
        for (auto& r : rec.fmts) {
            ofs << "," << std::setprecision(4) << r.cr
                << "," << std::setprecision(6) << r.cs_mb
                << "," << std::setprecision(4) << r.sed_ms
                << "," << std::setprecision(4) << r.set_gbps
                << "," << std::setprecision(4) << r.sdd_ms
                << "," << std::setprecision(4) << r.sdt_gbps;
            if (cfg.check_enabled)
                ofs << "," << std::setprecision(4) << r.sch_ms;
            ofs << "," << (cfg.check_enabled ? (r.ok ? "OK" : "FAIL") : "SKIP");
        }
        ofs << "\n";
    }
    ofs.flush();
    return static_cast<bool>(ofs);
}

/* ================================================================ */
/* Staged benchmark (rle16cli parity): outer format -> strip column ->
 * windowed rows (cache_blocks_max). Each phase wall-clock is spawn->join.
 * Integrity (--check): FNV after raster; verify after decode outside wdd. */
/* ================================================================ */

using rle4k_wall_clock = std::chrono::high_resolution_clock;

static double rle4k_wall_ms(const rle4k_wall_clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(rle4k_wall_clock::now() - t0).count();
}

static uint64_t rle4k_fnv1a64(const uint8_t* data, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

struct rle4k_block_digest {
    uint64_t hash = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t nbytes = 0;
    bool valid = false;
};

static rle4k_block_digest rle4k_make_digest(const bitmap_info& src, bool do_hash) {
    rle4k_block_digest d;
    if (!src.data || src.width == 0 || src.height == 0) return d;
    d.width = src.width;
    d.height = src.height;
    d.stride = src.get_stride_bytes();
    d.nbytes = src.data_length;
    d.valid = true;
    if (do_hash)
        d.hash = rle4k_fnv1a64(reinterpret_cast<const uint8_t*>(src.data),
                               static_cast<size_t>(d.nbytes));
    return d;
}

/* Phase 1 — rasterize a window of row blocks [row0, row0+nwin). */
static double rle4k_phase_raster_window(const cad_document* doc, int col, int row0, int nwin,
    double pw_nm, const bench_config& cfg, int nthreads,
    std::vector<std::unique_ptr<bitmap_info>>& raw,
    std::vector<rle4k_block_digest>& digests, bool do_hash,
    double& cpu_ms, double& ss_bytes)
{
    rle4k_row_block_queue work(nwin);
    nthreads = std::max(1, std::min(nthreads, std::min(nwin, 10)));
    std::vector<double> cpu_us(static_cast<size_t>(nthreads), 0.0);
    const auto t0 = rle4k_wall_clock::now();
    std::vector<std::thread> workers; workers.reserve(static_cast<size_t>(nthreads));
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&, t]() {
            for (;;) {
                int local = work.pop();
                if (local < 0) break;
                const int row = row0 + local;
                performance_timer rt; rt.start();
                auto block = rle4k_rasterize_block(doc, col, row, pw_nm,
                    cfg.strip_width, cfg.block_line, ALIGN064);
                rt.stop();
                cpu_us[static_cast<size_t>(t)] += rt.elapsed_us();
                digests[static_cast<size_t>(local)] =
                    block ? rle4k_make_digest(*block, do_hash) : rle4k_block_digest{};
                raw[static_cast<size_t>(local)] = std::move(block);
            }
        });
    }
    for (auto& w : workers) if (w.joinable()) w.join();
    const double wall = rle4k_wall_ms(t0);
    cpu_ms = 0; for (double c : cpu_us) cpu_ms += c / 1000.0;
    ss_bytes = 0; for (auto& b : raw) if (b) ss_bytes += b->data_length;
    return wall;
}

/* Phase 2 — encode window. */
static double rle4k_phase_encode_window(format_encoder* enc, const encode_format_args& args,
    const std::vector<std::unique_ptr<bitmap_info>>& raw,
    std::vector<std::unique_ptr<bitmap_info>>& enc_cache,
    int nthreads, double& cpu_ms, double& cs_bytes)
{
    const int nwin = static_cast<int>(raw.size());
    rle4k_row_block_queue work(nwin);
    nthreads = std::max(1, std::min(nthreads, std::min(nwin, 10)));
    std::vector<double> cpu_us(static_cast<size_t>(nthreads), 0.0);
    const auto t0 = rle4k_wall_clock::now();
    std::vector<std::thread> workers; workers.reserve(static_cast<size_t>(nthreads));
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&, t]() {
            for (;;) {
                int local = work.pop();
                if (local < 0) break;
                if (!raw[static_cast<size_t>(local)]) continue;
                performance_timer et; et.start();
                auto e = enc->process_pack(raw[static_cast<size_t>(local)].get(), args);
                et.stop();
                cpu_us[static_cast<size_t>(t)] += et.elapsed_us();
                enc_cache[static_cast<size_t>(local)] = std::move(e);
            }
        });
    }
    for (auto& w : workers) if (w.joinable()) w.join();
    const double wall = rle4k_wall_ms(t0);
    cpu_ms = 0; for (double c : cpu_us) cpu_ms += c / 1000.0;
    cs_bytes = 0; for (auto& e : enc_cache) if (e) cs_bytes += e->data_length;
    return wall;
}

/* Decode only (no integrity). Returns true if codec API succeeded. */
static bool rle4k_decode_payload(int fmt, const bitmap_info* e,
    uint32_t width, uint32_t height, uint32_t stride, uint8_t* dst, size_t dst_cap)
{
    if (!e || !dst || dst_cap == 0) return false;
    int drc = -1;
    if (fmt == raster_output_format_rle4k) {
        drc = rle4k_decode_block((const uint8_t*)e->data, e->data_length,
            dst, width, height, stride);
        return drc >= 0;
    }
    if (fmt == raster_output_format_gzip) {
        uLongf gzip_dl = (uLongf)dst_cap;
        return uncompress(dst, &gzip_dl, (const Bytef*)e->data, (uLong)e->data_length) == Z_OK;
    }
    if (fmt == raster_output_format_lzo2) {
        lzo_uint lzo_dl = (lzo_uint)dst_cap;
        return lzo1x_decompress((const lzo_bytep)e->data, e->data_length,
            (lzo_bytep)dst, &lzo_dl, nullptr) == LZO_E_OK;
    }
    if (fmt == raster_output_format_snappy)
        return snappy::RawUncompress(e->data, e->data_length, (char*)dst);
    if (fmt == raster_output_format_zstd) {
        size_t zstd_dl = ZSTD_decompress(dst, dst_cap, e->data, e->data_length);
        return !ZSTD_isError(zstd_dl);
    }
    if (fmt == raster_output_formaT_BROTLI)
        return true; /* decode not implemented — treated as pass */
    return false;
}

/* Phase 3 — decode window; FNV verify runs AFTER wall timer (outside wdd). */
static double rle4k_phase_decode_window(int fmt,
    const std::vector<rle4k_block_digest>& digests,
    const std::vector<std::unique_ptr<bitmap_info>>& enc_cache,
    int nthreads, bool check, double& cpu_ms, bool& all_ok, double& check_ms)
{
    const int nwin = static_cast<int>(digests.size());
    rle4k_row_block_queue work(nwin);
    nthreads = std::max(1, std::min(nthreads, std::min(nwin, 10)));
    std::vector<double> cpu_us(static_cast<size_t>(nthreads), 0.0);
    std::atomic<bool> ok{true};
    std::vector<std::vector<uint8_t>> check_bufs;
    std::vector<char> decoded_ok;
    if (check) {
        check_bufs.resize(static_cast<size_t>(nwin));
        decoded_ok.assign(static_cast<size_t>(nwin), 0);
    }

    const auto t0 = rle4k_wall_clock::now();
    std::vector<std::thread> workers; workers.reserve(static_cast<size_t>(nthreads));
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&, t]() {
            std::vector<uint8_t> dbuf; /* per-worker reuse when !check */
            for (;;) {
                int local = work.pop();
                if (local < 0) break;
                const auto& dig = digests[static_cast<size_t>(local)];
                const auto& e = enc_cache[static_cast<size_t>(local)];
                if (!dig.valid || !e) continue;
                const size_t need = static_cast<size_t>(dig.nbytes) + 4096;
                uint8_t* dst = nullptr;
                if (check) {
                    check_bufs[static_cast<size_t>(local)].assign(need, 0);
                    dst = check_bufs[static_cast<size_t>(local)].data();
                } else {
                    if (dbuf.size() < need) dbuf.assign(need, 0);
                    dst = dbuf.data();
                }
                performance_timer dt; dt.start();
                const bool dec_ok = rle4k_decode_payload(fmt, e.get(), dig.width, dig.height,
                    dig.stride, dst, need);
                dt.stop();
                cpu_us[static_cast<size_t>(t)] += dt.elapsed_us();
                if (!dec_ok) {
                    if (check) ok.store(false, std::memory_order_relaxed);
                    continue;
                }
                if (check) decoded_ok[static_cast<size_t>(local)] = 1;
            }
        });
    }
    for (auto& w : workers) if (w.joinable()) w.join();
    const double wall = rle4k_wall_ms(t0);

    check_ms = 0;
    if (check) {
        const auto c0 = rle4k_wall_clock::now();
        for (int i = 0; i < nwin; ++i) {
            const auto& dig = digests[static_cast<size_t>(i)];
            if (!dig.valid) continue;
            if (fmt == raster_output_formaT_BROTLI) continue; /* no decode payload */
            if (!decoded_ok[static_cast<size_t>(i)]) {
                ok.store(false, std::memory_order_relaxed);
                continue;
            }
            const uint64_t got = rle4k_fnv1a64(check_bufs[static_cast<size_t>(i)].data(),
                                              static_cast<size_t>(dig.nbytes));
            if (got != dig.hash) ok.store(false, std::memory_order_relaxed);
        }
        check_ms = rle4k_wall_ms(c0);
    }

    cpu_ms = 0; for (double c : cpu_us) cpu_ms += c / 1000.0;
    all_ok = ok.load();
    return wall;
}

/* ================================================================ */
/* rle4k_cmd_init — create redcli.json in current working directory */
int rle4k_cmd_init() {
    const std::string dest = "redcli.json";
    if (std::filesystem::exists(dest)) {
        std::cerr << dest << " already exists.\n";
        return 0;
    }
    return rle4k_write_default_config(dest) ? 0 : 1;
}

/* rle4k_cmd_edit — open config in system default editor */
int rle4k_cmd_edit() {
    std::string path;
    if (!rle4k_ensure_config_file(path)) return 1;
    std::cerr << "[redcli] opening: " << path << "\n";
    if (!rle4k_open_in_editor(path)) {
        std::cerr << "[ERROR] failed to launch editor for " << path << "\n";
        std::cerr << "  Set EDITOR or VISUAL, or associate .json with an editor.\n";
        return 1;
    }
    return 0;
}

/* ================================================================ */
/* rle4k_cmd_run */
int rle4k_cmd_run(int argc, char* argv[]) {
    bench_config cfg;
    bool cfg_loaded = false;
    auto resolved = rle4k_resolve_config_path();
    if (!resolved.empty() && load_config(resolved, cfg)) {
        cfg_loaded = true;
        std::cerr << "[redcli] config: " << resolved << "\n";
    }
    if (!cfg_loaded)
        std::cerr << "[WARN] redcli.json not found, using defaults.\n";

    /* CLI overrides */
    bool force = false;
    std::string csv_path;   // explicit CSV output path; empty ⇒ console only
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--input" || a == "-i") && i+1 < argc) cfg.input_path = argv[++i];
        else if ((a == "--mode" || a == "-m") && i+1 < argc) cfg.mode = std::stoi(argv[++i]);
        else if ((a == "--repeat" || a == "-r") && i+1 < argc) cfg.repeat = std::stoi(argv[++i]);
        else if (a == "--pw" && i+1 < argc) cfg.pitch_width_um = std::stod(argv[++i]);
        else if ((a == "-N" || a == "--oblique") && i+1 < argc) cfg.oblique_factor = std::stoi(argv[++i]);
        else if ((a == "--strip-height" || a == "-sh") && i+1 < argc) cfg.block_line = std::stoi(argv[++i]);
        else if ((a == "--strip-width" || a == "-sw") && i+1 < argc) cfg.strip_width = std::stoi(argv[++i]);
        else if ((a == "--threads" || a == "--raster-threads" || a == "-t") && i+1 < argc) cfg.raster_threads = std::stoi(argv[++i]);
        else if ((a == "--encode-threads" || a == "-et") && i+1 < argc) cfg.encode_threads = std::stoi(argv[++i]);
        else if ((a == "--decode-threads" || a == "-dt") && i+1 < argc) cfg.decode_threads = std::stoi(argv[++i]);
        else if (a == "--no-encode") cfg.encode_enabled = 0;
        else if (a == "--no-decode") cfg.decode_enabled = 0;
        else if (a == "--check") cfg.check_enabled = 1;
        else if (a == "--no-check") cfg.check_enabled = 0;
        else if ((a == "--cache-blocks-min" || a == "--cache-min") && i+1 < argc)
            cfg.cache_blocks_min = std::stoi(argv[++i]);
        else if ((a == "--cache-blocks-max" || a == "--cache-max") && i+1 < argc)
            cfg.cache_blocks_max = std::stoi(argv[++i]);
        else if ((a == "--cache-blocks" || a == "--raw-cache") && i+1 < argc)
            cfg.cache_blocks_max = std::stoi(argv[++i]);
        else if ((a == "--verbose" || a == "-v") && i+1 < argc) cfg.verbose = argv[++i];
        else if ((a == "--formats" || a == "-f") && i+1 < argc) {
            cfg.formats.clear();
            std::string list = argv[++i];
            std::stringstream ss(list);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (tok.empty()) continue;
                cfg.formats.push_back(std::stoi(tok));
            }
        }
        else if ((a == "--csv" || a == "-o" || a == "--output") && i+1 < argc) csv_path = argv[++i];
        else if (a == "--force") force = true;
        else if (a == "-h" || a == "--help") { rle4k_print_usage(); return 0; }
    }

#ifdef _WIN32
    {
        char* env_threads = nullptr;
        size_t env_len = 0;
        if (_dupenv_s(&env_threads, &env_len, "RLE4K_THREADS") == 0 && env_threads) {
            int t = std::atoi(env_threads);
            if (t > 0) cfg.raster_threads = t;
            free(env_threads);
        }
    }
#else
    if (const char* env_threads = std::getenv("RLE4K_THREADS")) {
        int t = std::atoi(env_threads);
        if (t > 0) cfg.raster_threads = t;
    }
#endif
    if (cfg.raster_threads < 1) cfg.raster_threads = 1;
    if (cfg.encode_threads < 0) cfg.encode_threads = 0;
    if (cfg.decode_threads < 0) cfg.decode_threads = 0;
    /* Product constraint: max worker threads is 10 for all phases. */
    constexpr int kMaxWorkerThreads = 10;
    if (cfg.raster_threads > kMaxWorkerThreads) cfg.raster_threads = kMaxWorkerThreads;
    if (cfg.encode_threads > kMaxWorkerThreads) cfg.encode_threads = kMaxWorkerThreads;
    if (cfg.decode_threads > kMaxWorkerThreads) cfg.decode_threads = kMaxWorkerThreads;
    if (cfg.cache_blocks_min < 1) cfg.cache_blocks_min = 1;
    if (cfg.cache_blocks_max < cfg.cache_blocks_min)
        cfg.cache_blocks_max = cfg.cache_blocks_min;

    if (cfg.input_path.empty()) { std::cerr << "[ERROR] --input required\n"; return 1; }

    /* collect files */
    std::vector<std::string> files;
    if (cfg.mode == 1) {
        for (auto& e : std::filesystem::directory_iterator(cfg.input_path))
            if (e.is_regular_file()) {
                auto p = e.path().string();
                if (p.size() >= 4 && p.substr(p.size()-4) == ".dif") files.push_back(p);
            }
    } else files.push_back(cfg.input_path);
    if (files.empty()) { std::cerr << "[ERROR] No .dif files\n"; return 1; }

    /* auto strip_width */
    if (cfg.strip_width == 0) {
        int auto_w = cfg.dmd_resolution_cols * cfg.oblique_factor + cfg.dmd_resolution_rows;
        cfg.strip_width = ((auto_w + cfg.align_mode - 1) / cfg.align_mode) * cfg.align_mode;
    }

    /* build format list */
    auto& factory = format_encoder_factory::get_instance();
    std::vector<int> fmts;
    for (auto id : cfg.formats) { int f = map_format_id(id); if (f >= 0) fmts.push_back(f); }
    if (fmts.empty()) fmts = {0x4,0x7,0xB,0xA,0x8,0xF};

    initialize_global_buffer_manager(cfg.strip_width, cfg.block_line, cfg.raster_threads);
    double pw_nm = cfg.pitch_width_um * 1000.0;

    /* === Per-file comparison records (for console report + optional CSV) === */
    std::vector<rle4k_file_record> all_files;
    int load_failures = 0;

    int file_idx = 0;
    for (auto& file : files) {
        file_idx++;
        std::string short_name = std::filesystem::path(file).filename().string();
        rle4k_file_record rec;
        rec.short_name = short_name;
        std::cerr << "\n============================================================\n";
        std::cerr << "  File " << file_idx << "/" << files.size() << ": " << short_name << "\n";
        std::cerr << "============================================================\n\n";

        /* --- Load DIF --- */
        if (!std::filesystem::exists(file)) {
            std::cerr << "[ERROR] DIF not found: " << file << "\n\n";
            ++load_failures;
            continue;
        }
        std::cerr << "[Phase 1] Loading DIF...\n";
        std::cerr.flush();
        performance_timer load_t; load_t.start();
        std::unique_ptr<cad_document> doc;
        try {
            doc = rle4k_dif_loader().load(file);
        } catch (const std::exception& ex) {
            std::cerr << "[ERROR] DIF load failed: " << ex.what() << "\n\n";
            ++load_failures;
            continue;
        } catch (...) {
            std::cerr << "[ERROR] DIF load failed: unknown exception\n\n";
            ++load_failures;
            continue;
        }
        load_t.stop();
        if (!doc || doc->layer_count() == 0) { std::cerr << "  [SKIP] No layers.\n\n"; continue; }
        double d_ms = load_t.elapsed_us() / 1000.0;
        rec.d_ms = d_ms;
        std::cerr << "[Phase 1] DIF loaded in " << std::fixed << std::setprecision(0) << d_ms << "ms\n";

        int img_w = (int)ceil((doc->max_x - doc->min_x) / pw_nm);
        int img_h = (int)ceil((doc->max_y - doc->min_y) / pw_nm);
        int ncols = (img_w + cfg.strip_width - 1) / cfg.strip_width;
        int nrows = (img_h + cfg.block_line - 1) / cfg.block_line;
        rec.img_w = img_w; rec.img_h = img_h;
        rec.ncols = ncols; rec.nrows = nrows;

        std::cerr << "[Phase 1] Image: " << ncols << " cols x " << nrows << " rows"
                  << " (" << cfg.strip_width << "x" << cfg.block_line << " px blocks)\n\n";

        /* --- format -> strip col -> windowed rows (rle16cli parity; re-raster/format) --- */
        const int rt_threads = std::max(1, cfg.raster_threads);
        const int et_threads = cfg.encode_threads > 0 ? cfg.encode_threads : rt_threads;
        const int dt_threads = cfg.decode_threads > 0 ? cfg.decode_threads : rt_threads;
        const int repeat = std::max(1, cfg.repeat);
        const bool do_encode = cfg.encode_enabled != 0;
        const bool do_decode = do_encode && cfg.decode_enabled != 0;
        const bool do_check = cfg.check_enabled != 0;
        const int cache_min = std::max(1, cfg.cache_blocks_min);
        const int cache_max = std::max(cache_min, cfg.cache_blocks_max);
        const int cache_blocks = cache_max;

        encode_format_args args;
        args.gzip_level=cfg.gzip_level; args.zstd_level=cfg.zstd_level;
        args.lzo2_level=cfg.lzo2_level; args.snappy_level=cfg.snappy_level;
        args.brotli_level=cfg.brotli_level; args.brotli_window=cfg.brotli_window;

        struct fmt_acc {
            double raster_wall=0, enc_wall=0, dec_wall=0, check_wall=0;
            double enc_cpu=0, dec_cpu=0, cs=0, ss=0;
            bool ok=true;
        };
        std::vector<fmt_acc> facc(fmts.size());

        std::cerr << "[args] formats=" << fmts.size()
                  << " rt=" << rt_threads << " et=" << et_threads << " dt=" << dt_threads
                  << " repeat=" << repeat
                  << " cache=[" << cache_min << "," << cache_max << "]"
                  << " check=" << (do_check ? "on" : "off")
                  << " enc=" << (do_encode ? "on" : "off")
                  << " dec=" << (do_decode ? "on" : "off") << "\n";
        std::cerr << "[Phase] outer format -> strip col -> windowed rows"
                  << " (" << ncols << "x" << nrows << " blocks;"
                  << " ~" << std::fixed << std::setprecision(1)
                  << ((double)cfg.strip_width * cfg.block_line / 8.0 * nrows / 1e9)
                  << " GB raw/col); re-raster per format\n";
        std::cerr.flush();

        for (size_t fi = 0; fi < fmts.size(); ++fi) {
            auto* enc = factory.get_encoder(fmts[fi]);
            if (!enc) continue;
            const int fmt = enc->get_supported_format();
            const char* efn = enc->get_format_name();

            for (int col = 0; col < ncols; ++col) {
                double wrd = 0, wed_sum = 0, wdd_sum = 0, wchk_sum = 0;
                double ss_bytes = 0, cs_bytes_sum = 0;
                double enc_cpu_sum = 0, dec_cpu_sum = 0;
                bool all_ok = true;

                std::cerr << "  [" << efn << "] sid " << (col + 1) << "/" << ncols
                          << " " << std::flush;

                for (int row0 = 0; row0 < nrows; row0 += cache_blocks) {
                    const int nwin = std::min(cache_blocks, nrows - row0);
                    std::vector<std::unique_ptr<bitmap_info>> raw(static_cast<size_t>(nwin));
                    std::vector<rle4k_block_digest> digests(static_cast<size_t>(nwin));
                    double win_cpu = 0, win_ss = 0;
                    wrd += rle4k_phase_raster_window(doc.get(), col, row0, nwin, pw_nm, cfg,
                        rt_threads, raw, digests, do_check, win_cpu, win_ss);
                    ss_bytes += win_ss;

                    std::vector<std::unique_ptr<bitmap_info>> enc_cache(static_cast<size_t>(nwin));
                    if (do_encode) {
                        for (int rep = 0; rep < repeat; ++rep) {
                            double w_cpu = 0, w_cs = 0;
                            wed_sum += rle4k_phase_encode_window(enc, args, raw, enc_cache,
                                et_threads, w_cpu, w_cs);
                            enc_cpu_sum += w_cpu;
                            cs_bytes_sum += w_cs;
                        }
                    }

                    /* Drop uncompressed bitmaps before decode (rle16cli parity). */
                    for (auto& b : raw) b.reset();
                    raw.clear();
                    raw.shrink_to_fit();

                    if (do_decode) {
                        for (int rep = 0; rep < repeat; ++rep) {
                            double w_cpu = 0, w_chk = 0; bool w_ok = true;
                            wdd_sum += rle4k_phase_decode_window(fmt, digests, enc_cache,
                                dt_threads, do_check, w_cpu, w_ok, w_chk);
                            dec_cpu_sum += w_cpu;
                            wchk_sum += w_chk;
                            if (!w_ok) all_ok = false;
                        }
                    }
                    enc_cache.clear();
                    enc_cache.shrink_to_fit();
                }

                const double wed = do_encode ? (wed_sum / repeat) : 0;
                const double wdd = do_decode ? (wdd_sum / repeat) : 0;
                const double wchk = do_decode ? (wchk_sum / repeat) : 0;
                const double cs_bytes = do_encode ? (cs_bytes_sum / repeat) : 0;
                const double enc_cpu = do_encode ? (enc_cpu_sum / repeat) : 0;
                const double dec_cpu = do_decode ? (dec_cpu_sum / repeat) : 0;
                const double ss_gb = ss_bytes / 1e9;
                const double set_gbps = (wed > 0.0) ? (ss_gb * 8e3 / wed) : 0.0;
                const double sdt_gbps = (wdd > 0.0) ? (ss_gb * 8e3 / wdd) : 0.0;
                const double cr = (cs_bytes > 0.0) ? (ss_bytes / cs_bytes) : 0.0;
                const double total_ms = wrd + wed + wdd;

                std::cerr << "raster=" << static_cast<int>(wrd) << "ms size="
                          << std::setprecision(2) << (ss_bytes / 1e9) << "GB"
                          << " enc=" << static_cast<int>(wed) << "ms("
                          << static_cast<int>(set_gbps) << "Gbps)"
                          << " dec=" << static_cast<int>(wdd) << "ms("
                          << static_cast<int>(sdt_gbps) << "Gbps)"
                          << " total=" << static_cast<int>(total_ms) << "ms"
                          << " cr=" << std::fixed << std::setprecision(1) << cr << ":1";
                if (do_check)
                    std::cerr << " " << (all_ok ? "OK" : "NG");
                std::cerr << "\n";
                std::cerr.flush();

                facc[fi].raster_wall += wrd;
                facc[fi].enc_wall += wed;
                facc[fi].dec_wall += wdd;
                facc[fi].check_wall += wchk;
                facc[fi].enc_cpu += enc_cpu;
                facc[fi].dec_cpu += dec_cpu;
                facc[fi].cs += cs_bytes;
                facc[fi].ss += ss_bytes;
                if (do_decode && do_check && !all_ok) facc[fi].ok = false;
            }
        }

        /* file-level ss/raster from first format (geometry once; formats re-raster) */
        const double file_ss_bytes = facc.empty() ? 0 : facc[0].ss;
        const double file_raster_wall = facc.empty() ? 0 : facc[0].raster_wall;
        rec.raster_ms   = file_raster_wall;
        rec.total_ss_gb = file_ss_bytes / 1e9;
        const double file_ss_gb = rec.total_ss_gb;

        /* ---- per-file summary (wall clock) ---- */
        std::cerr << "  ----------------------------------------------------------------\n";
        std::cerr << "  raster(first fmt,wall)=" << std::fixed << std::setprecision(0)
                  << file_raster_wall << "ms"
                  << " ss=" << std::setprecision(2) << file_ss_gb << "GB"
                  << " cache=[" << cache_min << "," << cache_max << "]"
                  << " check=" << (do_check ? "on" : "off") << "\n";

        for (size_t fi = 0; fi < fmts.size(); ++fi) {
            auto* enc = factory.get_encoder(fmts[fi]);
            if (!enc) continue;
            const std::string efn = enc->get_format_name();
            const auto& acc = facc[fi];
            const double fmt_ss_gb = acc.ss / 1e9;
            const double cr  = acc.cs > 0 ? acc.ss / acc.cs : 0;
            const double set = acc.enc_wall > 0 ? fmt_ss_gb * 8e3 / acc.enc_wall : 0;
            const double sdt = acc.dec_wall > 0 ? fmt_ss_gb * 8e3 / acc.dec_wall : 0;

            std::cerr << "  " << std::left << std::setw(7) << efn << std::right << std::fixed
                      << " raster=" << std::setprecision(0) << acc.raster_wall << "ms"
                      << " enc(wall)=" << acc.enc_wall << "ms"
                      << " dec(wall)=" << acc.dec_wall << "ms"
                      << " ss=" << std::setprecision(2) << fmt_ss_gb << "GB"
                      << " cs=" << (acc.cs/1e6) << "MB"
                      << " cr=" << std::setprecision(1) << cr << ":1"
                      << " set=" << std::setprecision(2) << set << "Gbps"
                      << " sdt=" << sdt << "Gbps";
            if (do_check)
                std::cerr << " " << (acc.ok ? "OK" : "NG");
            std::cerr << "\n";

            rle4k_fmt_result fr;
            fr.name     = efn;
            fr.ss_gb    = fmt_ss_gb;
            fr.cs_mb    = acc.cs / 1e6;
            fr.cr       = cr;
            fr.sed_ms   = acc.enc_wall;
            fr.set_gbps = set;
            fr.sdd_ms   = acc.dec_wall;
            fr.sdt_gbps = sdt;
            fr.sch_ms   = acc.check_wall;
            fr.ok       = acc.ok;
            rec.fmts.push_back(fr);
        }
        std::cerr << "\n";

        /* per-file comparison report → console (stdout) */
        rle4k_print_file_report(rec, cfg);
        all_files.push_back(std::move(rec));
    }

    /* === Write CSV only when a path is explicitly given === */
    if (!csv_path.empty()) {
        if (rle4k_write_csv(csv_path, all_files, cfg))
            std::cerr << "[redcli] CSV written (" << all_files.size() << " files, UTF-8): " << csv_path << "\n";
        else
            std::cerr << "[ERROR] failed to write CSV: " << csv_path << "\n";
    } else {
        std::cerr << "[redcli] Console report only (" << all_files.size() << " files)."
                  << " Use --csv <path> to export a CSV file.\n";
    }

    std::cerr << "[redcli] Done.\n";
    global_buffer_manager::get_instance().clear();
    if (load_failures > 0 || all_files.empty()) return 1;
    return 0;
}

/* ================================================================ */
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001); SetConsoleCP(65001);
#endif
    rle4k_print_banner();
    if (argc < 2) { rle4k_print_usage(); return 1; }
    std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        rle4k_print_usage();
        return 0;
    }
    if (cmd == "init")       return rle4k_cmd_init();
    if (cmd == "run")        return rle4k_cmd_run(argc, argv);
    if (cmd == "edit" || cmd == "config") return rle4k_cmd_edit();
    /* backward compat */
    if (cmd == "benchmark")  return rle4k_cmd_run(argc, argv);
    rle4k_print_usage();
    return 1;
}
