#pragma once
/*
 * Shared helpers for redcli unit tests.
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#include "rle4k_bitmap.h"
#include "rle4k_buffer_mgr.h"
#include "rle4k_dif_loader.h"

namespace rle4k_test {

inline std::filesystem::path temp_dir() {
    auto p = std::filesystem::temp_directory_path() / "rle4k_unit_tests";
    std::filesystem::create_directories(p);
    return p;
}

inline std::unique_ptr<rle4k::fill::bitmap_info> make_bitmap(
    uint32_t w, uint32_t h, uint8_t fill = 0,
    rle4k::fill::map_align_mode am = rle4k::fill::ALIGN008)
{
    rle4k::fill::initialize_global_buffer_manager(w, h, 1);
    auto bmp = std::make_unique<rle4k::fill::bitmap_info>(w, h, am);
    if (bmp->data && bmp->data_length > 0)
        std::memset(bmp->data, fill, bmp->data_length);
    return bmp;
}

inline std::unique_ptr<rle4k::fill::bitmap_info> make_pattern_bitmap(
    uint32_t w, uint32_t h, int pattern)
{
    auto bmp = make_bitmap(w, h, 0);
    uint32_t stride = bmp->get_stride_bytes();
    for (uint32_t y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint8_t*>(bmp->data) + y * stride;
        for (uint32_t x = 0; x < w; ++x) {
            bool on = false;
            switch (pattern) {
            case 0: on = false; break;                         /* all white */
            case 1: on = true; break;                          /* all black */
            case 2: on = ((x / 8) + (y / 8)) % 2 == 0; break;  /* checker */
            case 3: on = (x % 17) < 9; break;                  /* vertical runs */
            case 4: on = (y % 13) < 7; break;                  /* horizontal runs */
            case 5: on = ((x * 3 + y * 7) % 11) < 5; break;    /* pseudo-random */
            default: on = (x & 1) != 0; break;
            }
            if (on)
                row[x / 8] |= static_cast<uint8_t>(1u << (x % 8));
        }
    }
    return bmp;
}

/* Write a minimal DIF_DOUBLE_V10 (VR=1.0) with one layer and one axis-aligned rectangle. */
inline std::string write_minimal_dif(const std::filesystem::path& path,
                                     double sx = 0, double sy = 0,
                                     double iw = 1000000, double ih = 1000000)
{
#pragma pack(push, 4)
    struct dif_int_header    { char name[16]; int value;    char memo[64]; };
    struct dif_double_header { char name[16]; double value; char memo[64]; };
    struct dif_string_header { char name[16]; char value[512]; char memo[64]; };
    struct dif_layer_header  { int id; char name[64]; int pc; int pol; char obj[16]; double bx[4]; };
    struct dif_poly_dheader  { int len, lid, pid, vc, holes; double mx, my, Mx, My, area; int start, fin; };
#pragma pack(pop)

    enum { INT_HEADER_COUNT = 40, DOUBLE_HEADER_COUNT = 40, STRING_HEADER_COUNT = 40, MAX_LAYERS = 4096 };
    const int LAYER_START_POS = INT_HEADER_COUNT * 84 + DOUBLE_HEADER_COUNT * 88 + STRING_HEADER_COUNT * 592;
    const int POLYGON_START_POS = LAYER_START_POS + MAX_LAYERS * 124;

    std::vector<char> buf(static_cast<size_t>(POLYGON_START_POS) + 256, 0);

    auto set_int = [&](int idx, const char* name, int value) {
        auto* h = reinterpret_cast<dif_int_header*>(buf.data() + idx * 84);
        std::strncpy(h->name, name, 15);
        h->value = value;
    };
    auto set_dbl = [&](int idx, const char* name, double value) {
        auto* h = reinterpret_cast<dif_double_header*>(
            buf.data() + INT_HEADER_COUNT * 84 + idx * 88);
        std::strncpy(h->name, name, 15);
        h->value = value;
    };
    auto set_str = [&](int idx, const char* name, const char* value) {
        auto* h = reinterpret_cast<dif_string_header*>(
            buf.data() + INT_HEADER_COUNT * 84 + DOUBLE_HEADER_COUNT * 88 + idx * 592);
        std::strncpy(h->name, name, 15);
        std::strncpy(h->value, value, 511);
    };

    set_int(0, "LC", 1);
    set_int(1, "PC", 1);
    set_int(2, "MO", 0); /* NM */
    set_int(3, "SU", 1);

    set_dbl(0, "SX", sx);
    set_dbl(1, "SY", sy);
    set_dbl(2, "IW", iw);
    set_dbl(3, "IH", ih);

    set_str(0, "VR", "1.0");

    auto* lh = reinterpret_cast<dif_layer_header*>(buf.data() + LAYER_START_POS);
    lh->id = 0;
    std::strncpy(lh->name, "L0", 63);
    lh->pc = 1;
    lh->pol = 1; /* DIF dark/draw — matches production artwork layers */
    lh->bx[0] = sx; lh->bx[1] = sy; lh->bx[2] = iw; lh->bx[3] = ih;

    /* rectangle: (100k,100k)-(900k,900k) in nm document units */
    const double x0 = sx + (iw - sx) * 0.1;
    const double y0 = sy + (ih - sy) * 0.1;
    const double x1 = sx + (iw - sx) * 0.9;
    const double y1 = sy + (ih - sy) * 0.9;
    const int vc = 4;
    const int ph_sz = 68;
    const int poly_len = ph_sz + vc * 16;

    auto* ph = reinterpret_cast<dif_poly_dheader*>(buf.data() + POLYGON_START_POS);
    ph->len = poly_len;
    ph->lid = 0;
    ph->pid = 0;
    ph->vc = vc;
    ph->holes = 0;
    ph->mx = x0; ph->my = y0; ph->Mx = x1; ph->My = y1;
    ph->area = (x1 - x0) * (y1 - y0);
    ph->start = 0;
    ph->fin = 0;

    double verts[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
    std::memcpy(buf.data() + POLYGON_START_POS + ph_sz, verts, sizeof(verts));

    buf.resize(static_cast<size_t>(POLYGON_START_POS) + static_cast<size_t>(poly_len));

    std::ofstream f(path, std::ios::binary);
    f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    f.close();
    return path.string();
}

} // namespace rle4k_test

#ifdef _MSC_VER
#pragma warning(pop)
#endif
