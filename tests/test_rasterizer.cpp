#include <gtest/gtest.h>
#include <algorithm>

#include "rle4k_rasterizer.h"
#include "rle4k_dif_loader.h"
#include "test_helpers.h"

USING_SP2_NAMESPACE();
USING_CAM_NAMESPACE();

TEST(Rasterizer, RasterizeMinimalBlock) {
    auto path = rle4k_test::temp_dir() / "raster.dif";
    /* 1mm x 1mm document in nm */
    rle4k_test::write_minimal_dif(path, 0, 0, 1e6, 1e6);

    auto doc = load_cad_file(path.string());
    ASSERT_NE(doc, nullptr);

    /* pitch 10 um = 10000 nm ¡ú image ~100 x 100 px */
    const double pw_nm = 10000.0;
    const uint32_t sw = 128;
    const uint32_t sh = 64;
    const uint32_t img_w = (uint32_t)((doc->max_x - doc->min_x) / pw_nm + 1);
    const uint32_t img_h = (uint32_t)((doc->max_y - doc->min_y) / pw_nm + 1);
    const uint32_t expect_w = (std::min)(sw, img_w);
    const uint32_t expect_h = (std::min)(sh, img_h);

    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, pw_nm, sw, sh, ALIGN008);
    ASSERT_NE(bmp, nullptr);
    EXPECT_EQ(bmp->width, expect_w);
    EXPECT_EQ(bmp->height, expect_h);
    EXPECT_NE(bmp->data, nullptr);

    /* rectangle covers most of the block ¡ª expect some set bits */
    size_t ones = 0;
    for (uint32_t i = 0; i < bmp->data_length; ++i)
        ones += static_cast<unsigned char>(bmp->data[i]) != 0;
    EXPECT_GT(ones, 0u);
}

TEST(Rasterizer, OutOfBoundsReturnsNull) {
    auto path = rle4k_test::temp_dir() / "oob.dif";
    rle4k_test::write_minimal_dif(path);
    auto doc = load_cad_file(path.string());
    const double pw_nm = 10000.0;
    const uint32_t sw = 128;
    const uint32_t sh = 64;
    const uint32_t img_w = (uint32_t)((doc->max_x - doc->min_x) / pw_nm + 1);
    const uint32_t img_h = (uint32_t)((doc->max_y - doc->min_y) / pw_nm + 1);
    /* First column/row index that is fully past the image */
    int col = static_cast<int>((img_w + sw - 1) / sw);
    int row = static_cast<int>((img_h + sh - 1) / sh);

    auto bmp = rle4k_rasterize_block(doc.get(), col, row, pw_nm, sw, sh, ALIGN008);
    EXPECT_EQ(bmp, nullptr);
}

TEST(Rasterizer, MultipleBlocks) {
    auto path = rle4k_test::temp_dir() / "multi.dif";
    rle4k_test::write_minimal_dif(path, 0, 0, 2e6, 2e6);
    auto doc = load_cad_file(path.string());
    const double pw_nm = 10000.0;
    const uint32_t sw = 128;
    const uint32_t sh = 64;
    const uint32_t img_w = (uint32_t)((doc->max_x - doc->min_x) / pw_nm + 1);
    const uint32_t img_h = (uint32_t)((doc->max_y - doc->min_y) / pw_nm + 1);

    int produced = 0;
    for (int col = 0; col < 2; ++col) {
        for (int row = 0; row < 2; ++row) {
            auto bmp = rle4k_rasterize_block(doc.get(), col, row, pw_nm, sw, sh, ALIGN064);
            if (!bmp) continue;
            ++produced;
            uint32_t expect_w = (std::min)(sw, img_w - static_cast<uint32_t>(col) * sw);
            uint32_t expect_h = (std::min)(sh, img_h - static_cast<uint32_t>(row) * sh);
            EXPECT_EQ(bmp->width, expect_w);
            EXPECT_EQ(bmp->height, expect_h);
        }
    }
    EXPECT_GT(produced, 0);
}

TEST(Rasterizer, EmptyLayerStillAllocates) {
    cad_document doc;
    doc.min_x = 0; doc.min_y = 0;
    doc.max_x = 1e6; doc.max_y = 1e6;
    doc.pixel_size_nm = 10000;
    auto layer = std::make_unique<cad_layer>();
    layer->layer_id = 0;
    layer->polarity = true;
    doc.add_layer(std::move(layer));

    auto bmp = rle4k_rasterize_block(&doc, 0, 0, 10000.0, 64, 32, ALIGN008);
    ASSERT_NE(bmp, nullptr);
    EXPECT_EQ(bmp->width, 64u);
    /* empty geometry ? all zeros */
    bool any = false;
    for (uint32_t i = 0; i < bmp->data_length; ++i)
        if (bmp->data[i]) { any = true; break; }
    EXPECT_FALSE(any);
}

TEST(Rasterizer, ClearPolarityLayer) {
    auto path = rle4k_test::temp_dir() / "clear_pol.dif";
    rle4k_test::write_minimal_dif(path, 0, 0, 1e6, 1e6);
    auto doc = load_cad_file(path.string());
    ASSERT_NE(doc->get_layer(0), nullptr);
    /* invert polarity — clear spans instead of dark fill */
    const_cast<cad_layer*>(doc->get_layer(0))->polarity = false;
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 64, 64, ALIGN008);
    ASSERT_NE(bmp, nullptr);
}

namespace {

std::unique_ptr<cad_document> make_doc_with_poly(
    std::vector<cad_vertex> verts, bool polarity = true,
    double min_x = 0, double min_y = 0, double max_x = 1e6, double max_y = 1e6)
{
    auto doc = std::make_unique<cad_document>();
    doc->min_x = min_x; doc->min_y = min_y;
    doc->max_x = max_x; doc->max_y = max_y;
    doc->pixel_size_nm = 10000;
    auto layer = std::make_unique<cad_layer>();
    layer->layer_id = 0;
    layer->polarity = polarity;
    auto poly = std::make_unique<cad_polygon>();
    poly->polarity = polarity;
    poly->vertices = std::move(verts);
    double bx0 = poly->vertices[0].x, by0 = poly->vertices[0].y;
    double bx1 = bx0, by1 = by0;
    for (auto& v : poly->vertices) {
        bx0 = (std::min)(bx0, v.x); by0 = (std::min)(by0, v.y);
        bx1 = (std::max)(bx1, v.x); by1 = (std::max)(by1, v.y);
    }
    poly->bbox_min_x = bx0; poly->bbox_min_y = by0;
    poly->bbox_max_x = bx1; poly->bbox_max_y = by1;
    layer->add_polygon(std::move(poly));
    layer->build_grid(100000.0);
    doc->add_layer(std::move(layer));
    return doc;
}

} // namespace

TEST(Rasterizer, TrianglePolygon) {
    /* Three-vertex path in rle4k_build_polygon_edges (may be degenerate fill). */
    auto doc = make_doc_with_poly({
        {1e5, 1e5}, {9e5, 1e5}, {5e5, 9e5}
    });
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp, nullptr);

    /* Quad that is visually triangular-ish still fills */
    auto doc2 = make_doc_with_poly({
        {1e5, 1e5}, {9e5, 1e5}, {9e5, 1e5 + 10000}, {5e5, 9e5}
    });
    auto bmp2 = rle4k_rasterize_block(doc2.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp2, nullptr);
    size_t ones = 0;
    for (uint32_t i = 0; i < bmp2->data_length; ++i)
        ones += static_cast<unsigned char>(bmp2->data[i]) != 0;
    EXPECT_GT(ones, 0u);
}

TEST(Rasterizer, HorizontalDegeneratePolygon) {
    /* All vertices share the same Y after pixelization ? horizontal path */
    auto doc = make_doc_with_poly({
        {1e5, 5e5}, {3e5, 5e5}, {7e5, 5e5}, {9e5, 5e5}
    });
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp, nullptr);
}

TEST(Rasterizer, NonMonotonicPolygon) {
    /* Zigzag / concave outline exercises monotonicity group breaks */
    auto doc = make_doc_with_poly({
        {1e5, 1e5}, {9e5, 1e5}, {9e5, 5e5}, {5e5, 3e5},
        {9e5, 9e5}, {1e5, 9e5}, {1e5, 5e5}, {4e5, 6e5}
    });
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp, nullptr);
}

TEST(Rasterizer, PolygonClippedToBlock) {
    /* Polygon larger than block — edges clipped at blockMinY/MaxY */
    auto doc = make_doc_with_poly({
        {-5e5, -5e5}, {1.5e6, -5e5}, {1.5e6, 1.5e6}, {-5e5, 1.5e6}
    });
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 64, 64, ALIGN008);
    ASSERT_NE(bmp, nullptr);
}

TEST(Rasterizer, ClearAfterDarkSpans) {
    /* Dark fill then clear polarity on overlapping poly ? clear span path */
    auto doc = std::make_unique<cad_document>();
    doc->min_x = 0; doc->min_y = 0; doc->max_x = 1e6; doc->max_y = 1e6;
    doc->pixel_size_nm = 10000;

    auto dark = std::make_unique<cad_layer>();
    dark->layer_id = 0; dark->polarity = true;
    auto p1 = std::make_unique<cad_polygon>();
    p1->vertices = {{1e5, 1e5}, {9e5, 1e5}, {9e5, 9e5}, {1e5, 9e5}};
    p1->bbox_min_x = 1e5; p1->bbox_min_y = 1e5; p1->bbox_max_x = 9e5; p1->bbox_max_y = 9e5;
    dark->add_polygon(std::move(p1));
    dark->build_grid(100000.0);
    doc->add_layer(std::move(dark));

    auto clear = std::make_unique<cad_layer>();
    clear->layer_id = 1; clear->polarity = false;
    auto p2 = std::make_unique<cad_polygon>();
    p2->vertices = {{3e5, 3e5}, {7e5, 3e5}, {7e5, 7e5}, {3e5, 7e5}};
    p2->bbox_min_x = 3e5; p2->bbox_min_y = 3e5; p2->bbox_max_x = 7e5; p2->bbox_max_y = 7e5;
    clear->add_polygon(std::move(p2));
    clear->build_grid(100000.0);
    doc->add_layer(std::move(clear));

    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp, nullptr);
}

TEST(Rasterizer, NegativeColRowRejected) {
    cad_document doc;
    doc.min_x = 0; doc.min_y = 0; doc.max_x = 1e6; doc.max_y = 1e6;
    EXPECT_EQ(rle4k_rasterize_block(&doc, -1, 0, 10000.0, 64, 64, ALIGN008), nullptr);
    EXPECT_EQ(rle4k_rasterize_block(&doc, 0, -1, 10000.0, 64, 64, ALIGN008), nullptr);
}

TEST(Rasterizer, DuplicateVerticesAndTinyPoly) {
    auto doc = make_doc_with_poly({
        {2e5, 2e5}, {2e5, 2e5}, {8e5, 2e5}, {8e5, 2e5}, {8e5, 8e5}, {2e5, 8e5}
    });
    auto bmp = rle4k_rasterize_block(doc.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp, nullptr);

    auto thin = make_doc_with_poly({{1e5, 1e5}, {1e5 + 1, 1e5}, {1e5, 1e5 + 1}});
    auto bmp2 = rle4k_rasterize_block(thin.get(), 0, 0, 10000.0, 128, 128, ALIGN008);
    ASSERT_NE(bmp2, nullptr);
}
