#pragma once
/*
 * redcli configuration — minimal JSON parser and benchmark parameters
 */
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

struct bench_config {
    std::string input_path;
    std::string output_path;       /* CSV output dir */
    int    mode           = 0;     /* 0=file, 1=folder */
    int    repeat         = 3;
    double pitch_width_um = 0.5;
    int    oblique_factor  = 32;   /* N */
    int    raster_threads  = 4;    /* phase 1 raster worker count */
    int    encode_threads  = 0;    /* phase 2 encode worker count (0 = raster_threads) */
    int    decode_threads  = 0;    /* phase 3 decode worker count (0 = raster_threads) */
    int    encode_enabled  = 1;    /* action switch: run encode phase (0 = skip) */
    int    decode_enabled  = 1;    /* action switch: run decode phase (0 = skip) */
    int    block_line      = 512;    /* stripe height rows */
    int    strip_width     = 0;      /* stripe width px, 0=ALIGN(cols*N+rows, align) */
    int    align_mode      = 512;    /* bit alignment: 8,32,64,512 etc */
    int    dmd_resolution_rows = 512;
    int    dmd_resolution_cols = 1920;
    std::vector<int> formats;       /* format IDs: 0=RLE4K,1=GZIP,2=LZO2,3=SNAPPY,4=ZSTD,5=Brotli*/
    std::string verbose = "file";    /* "file" = per-file summary only, "strip" = per-stripe detail */

    /* per-format compression parameters (defaults from raster/libcodec/encoding/) */
    int gzip_level    = 6;      /* GZIP: 1..9, balanced speed/ratio  */
    int zstd_level    = 3;      /* ZSTD: 1..22, from ZSTDFormatEncoder.cpp:26  */
    int lzo2_level    = 1;      /* LZO2: 1=lzo1x_1_compress (fixed), 2..999=lzo1x_999  */
    int snappy_level  = 0;      /* SNAPPY: no level param, reserved  */
    int brotli_level  = 6;      /* BROTLI: 0..11, from BrotliFormatEncoder.cpp:25  */
    int brotli_window = 22;     /* BROTLI: 10..24, BROTLI_DEFAULT_WINDOW */
    int check_enabled = 0;      /* 1 = FNV after raster + verify outside decode wall; 0 = skip (default) */
    /* Raw-block watermark: window size = max; min reserved for fill/backpressure docs. */
    int cache_blocks_min = 32;
    int cache_blocks_max = 128;
};

/* Format ID map: config order → raster_output_format enum */
/*  config ID  0=RLE4K, 1=GZIP, 2=LZO2, 3=SNAPPY, 4=ZSTD, 5=Brotli  */
inline int map_format_id(int cfg_id) {
    static const int lut[] = {
        0x4,   /* RLE4K  */
        0x7,   /* GZIP   */
        0xB,   /* LZO2   */
        0xA,   /* SNAPPY */
        0x8,   /* ZSTD   */
        0xF,   /* Brotli*/
    };
    if (cfg_id < 0 || cfg_id >= (int)(sizeof(lut)/sizeof(lut[0]))) return -1;
    return lut[cfg_id];
}

/* Minimal JSON parser: int arrays */
inline std::vector<int> json_get_int_array(const std::string& json, const char* key) {
    std::vector<int> out;
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos + pat.size());
    if (pos == std::string::npos) return out;
    auto end = json.find(']', pos);
    if (end == std::string::npos) return out;
    std::string inner = json.substr(pos + 1, end - pos - 1);
    size_t i = 0;
    while (i < inner.size()) {
        while (i < inner.size() && (inner[i] == ' ' || inner[i] == '\t' || inner[i] == '\r' || inner[i] == '\n' || inner[i] == ',')) i++;
        if (i >= inner.size()) break;
        size_t j = i;
        while (j < inner.size() && inner[j] >= '0' && inner[j] <= '9') j++;
        if (j > i) { out.push_back(std::atoi(inner.substr(i, j - i).c_str())); i = j; }
        else i++;
    }
    return out;
}

/* helper: skip // comment line after pos, return first pos of next line */
inline size_t skip_comment(const std::string& s, size_t p) {
    if (p + 1 < s.size() && s[p] == '/' && s[p+1] == '/')
        return s.find('\n', p);
    return p;
}

/* Minimal JSON parser: top-level string/int/double only */
inline std::string json_get_str(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    /* skip whitespace + comment */
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    pos = skip_comment(json, pos);
    if (pos == std::string::npos) return "";
    /* value must be quoted */
    if (json[pos] != '"') return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}
inline int json_get_int(const std::string& json, const char* key, int def = 0) {
    /* skip string check for ints --- all our ints are unquoted */
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    pos = skip_comment(json, pos);
    if (pos == std::string::npos) return def;
    auto end = json.find_first_of(",}\r\n", pos);
    return std::atoi(json.substr(pos, end - pos).c_str());
}
inline double json_get_double(const std::string& json, const char* key, double def = 0.0) {
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    pos = skip_comment(json, pos);
    if (pos == std::string::npos) return def;
    auto end = json.find_first_of(",}\r\n", pos);
    return std::atof(json.substr(pos, end - pos).c_str());
}

inline bool load_config(const std::string& path, bench_config& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss; ss << f.rdbuf();
    std::string json = ss.str();
    cfg.input_path   = json_get_str(json, "input");
    cfg.output_path  = json_get_str(json, "output_path");
    cfg.mode         = json_get_int(json, "mode");
    cfg.repeat       = json_get_int(json, "repeat");
    cfg.pitch_width_um= json_get_double(json, "pitch_width_um");
    cfg.oblique_factor= json_get_int(json, "oblique_factor");
    cfg.raster_threads= json_get_int(json, "raster_threads");
    cfg.encode_threads= json_get_int(json, "encode_threads", 0);
    cfg.decode_threads= json_get_int(json, "decode_threads", 0);
    cfg.encode_enabled= json_get_int(json, "encode_enabled", 1);
    cfg.decode_enabled= json_get_int(json, "decode_enabled", 1);
    cfg.block_line   = json_get_int(json, "block_line");
    cfg.strip_width  = json_get_int(json, "strip_width");
    cfg.align_mode   = json_get_int(json, "align_mode");
    cfg.dmd_resolution_rows = json_get_int(json, "dmd_resolution_rows");
    cfg.dmd_resolution_cols = json_get_int(json, "dmd_resolution_cols");
    auto fmts = json_get_int_array(json, "formats");
    if (!fmts.empty()) cfg.formats = fmts;
    cfg.gzip_level    = json_get_int(json, "gzip_level");
    cfg.zstd_level    = json_get_int(json, "zstd_level");
    cfg.lzo2_level    = json_get_int(json, "lzo2_level");
    cfg.snappy_level  = json_get_int(json, "snappy_level");
    cfg.brotli_level  = json_get_int(json, "brotli_level");
    cfg.brotli_window = json_get_int(json, "brotli_window");
    cfg.check_enabled = json_get_int(json, "check_enabled", 0);
    cfg.cache_blocks_min = json_get_int(json, "cache_blocks_min", 32);
    cfg.cache_blocks_max = json_get_int(json, "cache_blocks_max", 128);
    cfg.verbose       = json_get_str(json, "verbose");
    if (cfg.verbose.empty()) cfg.verbose = "file";
    return true;
}

inline std::string rle4k_exe_directory() {
#ifdef _WIN32
    char exe[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe, sizeof(exe)) == 0) return ".";
    return std::filesystem::path(exe).parent_path().string();
#else
    return ".";
#endif
}

/* Search roots for redcli.json. */
inline std::vector<std::string> rle4k_config_search_dirs() {
    std::vector<std::string> dirs;
    dirs.push_back(".");
    dirs.push_back("..");
    auto exe_dir = rle4k_exe_directory();
    if (!exe_dir.empty() && exe_dir != ".") {
        dirs.push_back(exe_dir);
        dirs.push_back(exe_dir + "/..");
        dirs.push_back(exe_dir + "/../..");
    }
    dirs.push_back("docs");
    dirs.push_back("../docs");
    return dirs;
}

/* Built-in default config (source of truth for `redcli init`).
 * Keep docs/redcli.example.json in sync for human-readable reference. */
inline const char* rle4k_builtin_default_config_json() {
    return R"JSON({
    // ============================================================
    //  redcli.json — built-in default (redcli init)
    //  Field guide: docs/redcli.help.md  |  mirror: docs/redcli.example.json
    //  Comments (//) are for humans; the minimal parser accepts them.
    // ============================================================

    // Path to one .dif (mode 0) or a folder of *.dif (mode 1). Relative to CWD.
    "input": "",

    // 0 = single file; 1 = non-recursive folder scan for *.dif
    "mode": 0,

    // Conventional output dir (created if missing). CSV needs: --csv <file>
    "output_path": "build/out",

    // Encode/decode timing repeats (averaged). Higher = stabler, slower.
    "repeat": 3,

    // 0 = no check; 1 = FNV after raster + verify OUTSIDE decode wall timer
    "check_enabled": 0,

    // Sliding row-block window: size = cache_blocks_max (max raised to min if needed)
    "cache_blocks_min": 32,
    "cache_blocks_max": 128,

    // Pixel pitch in micrometres. pw_nm = pitch_width_um * 1000. Paper: 0.5
    "pitch_width_um": 0.5,

    // Oblique N. When strip_width==0:
    //   strip_width = ceil((cols*N + rows) / align_mode) * align_mode
    // Defaults → 1920*32+512 = 61952 px. Paper: 32
    "oblique_factor": 32,

    // Nominal DMD size for auto strip_width only (not DIF bounds)
    "dmd_resolution_rows": 512,
    "dmd_resolution_cols": 1920,

    // Strip width px. 0 = auto (formula above). >0 = force
    "strip_width": 0,

    // Rounding grain for auto strip_width. Paper: 512
    "align_mode": 512,

    // Row-block height in pixel rows. Paper: 512
    "block_line": 512,

    // Phase-1 raster workers (clamped 1..10). Env RLE4K_THREADS overrides if >0
    "raster_threads": 4,

    // Phase-2 encode workers. 0 = same as raster_threads (clamped 1..10)
    "encode_threads": 0,

    // Phase-3 decode workers. 0 = same as raster_threads (clamped 1..10)
    "decode_threads": 0,

    // 1 = run phase; 0 = skip. encode_enabled=0 also skips decode
    "encode_enabled": 1,
    "decode_enabled": 1,

    // 0=RLE4K 1=GZIP 2=LZO2 3=SNAPPY 4=ZSTD 5=BROTLI
    "formats": [0, 1, 2, 3, 4, 5],

    // zlib 1..9. Paper: 6
    "gzip_level": 6,
    // Zstd 1..22. Paper: 3
    "zstd_level": 3,
    // 1=lzo1x_1; 2..999=lzo1x_999. Paper: 1
    "lzo2_level": 1,
    // Reserved (Snappy has no level)
    "snappy_level": 0,
    // Brotli quality 0..11. Paper: 6
    "brotli_level": 6,
    // Brotli window bits 10..24. Paper: 22
    "brotli_window": 22,

    // "file" = per-file; "strip" = per-strip detail
    "verbose": "file"

}
)JSON";
}

/* Resolve existing redcli.json; empty if not found. */
inline std::string rle4k_resolve_config_path() {
    for (const auto& dir : rle4k_config_search_dirs()) {
        auto p = dir + "/redcli.json";
        if (std::filesystem::exists(p)) return p;
    }
    return "";
}

/* Preferred path to create a new redcli.json (exe dir, else CWD). */
inline std::string rle4k_default_config_dest() {
    auto exe_dir = rle4k_exe_directory();
    if (!exe_dir.empty() && exe_dir != ".") return exe_dir + "/redcli.json";
    return "redcli.json";
}

inline bool rle4k_write_default_config(const std::string& path) {
    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[ERROR] failed to write " << path << "\n";
        return false;
    }
    ofs << rle4k_builtin_default_config_json();
    ofs.flush();
    if (!ofs) {
        std::cerr << "[ERROR] failed to write " << path << "\n";
        return false;
    }
    std::cerr << "Created: " << path << " (built-in default)\n";
    return true;
}

inline bool rle4k_ensure_config_file(std::string& out_path) {
    out_path = rle4k_resolve_config_path();
    if (!out_path.empty()) return true;
    out_path = rle4k_default_config_dest();
    return rle4k_write_default_config(out_path);
}

inline bool rle4k_open_in_editor(const std::string& path) {
    std::string editor;
#ifdef _WIN32
    {
        char* env = nullptr;
        size_t len = 0;
        if (_dupenv_s(&env, &len, "VISUAL") == 0 && env && env[0]) editor = env;
        else {
            free(env);
            env = nullptr;
            if (_dupenv_s(&env, &len, "EDITOR") == 0 && env && env[0]) editor = env;
        }
        if (env) free(env);
    }
#else
    if (const char* v = std::getenv("VISUAL"); v && v[0]) editor = v;
    else if (const char* e = std::getenv("EDITOR"); e && e[0]) editor = e;
#endif

#ifdef _WIN32
    if (!editor.empty()) {
        std::string cmd = "\"" + editor + "\" \"" + path + "\"";
        return std::system(cmd.c_str()) == 0;
    }
    std::wstring wpath(path.begin(), path.end());
    HINSTANCE r = ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOW);
    return reinterpret_cast<intptr_t>(r) > 32;
#else
    std::string cmd;
    if (!editor.empty())
        cmd = "\"" + editor + "\" \"" + path + "\"";
    else
        cmd = "xdg-open \"" + path + "\" 2>/dev/null || open \"" + path + "\"";
    return std::system(cmd.c_str()) == 0;
#endif
}
