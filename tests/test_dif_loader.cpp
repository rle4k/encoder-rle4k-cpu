#include <gtest/gtest.h>

#include "rle4k_dif_loader.h"
#include "test_helpers.h"

USING_CAM_NAMESPACE();

TEST(DifLoader, DetectFileType) {
    EXPECT_EQ(detect_file_type("a.dif"), cad_file_type::dif);
    EXPECT_EQ(detect_file_type("A.DIF"), cad_file_type::dif);
    EXPECT_EQ(detect_file_type("x.gbr"), cad_file_type::unknown);
    EXPECT_EQ(detect_file_type("noext"), cad_file_type::unknown);
}

TEST(DifLoader, CreateLoader) {
    auto loader = create_cad_loader(cad_file_type::dif);
    ASSERT_NE(loader, nullptr);
    EXPECT_EQ(loader->get_type(), cad_file_type::dif);
    EXPECT_THROW(create_cad_loader(cad_file_type::unknown), std::runtime_error);
}

TEST(DifLoader, LoadMinimalDocument) {
    auto path = rle4k_test::temp_dir() / "minimal.dif";
    rle4k_test::write_minimal_dif(path, 0, 0, 1000000, 1000000);

    auto doc = load_cad_file(path.string());
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->layer_count(), 1u);
    EXPECT_EQ(doc->total_polygon_count(), 1u);
    EXPECT_DOUBLE_EQ(doc->min_x, 0.0);
    EXPECT_DOUBLE_EQ(doc->max_x, 1000000.0);

    const cad_layer* layer = doc->get_layer(0);
    ASSERT_NE(layer, nullptr);
    EXPECT_TRUE(layer->polarity); /* pol=1 in fixture → dark/draw */
    EXPECT_FALSE(layer->grid.empty());
    EXPECT_EQ(layer->polygon_count(), 1u);
    EXPECT_EQ(layer->polygons[0]->vertex_count(), 4u);
}

TEST(DifLoader, SpatialQuery) {
    auto path = rle4k_test::temp_dir() / "query.dif";
    rle4k_test::write_minimal_dif(path);

    auto doc = load_cad_file(path.string());
    auto* layer = doc->get_layer(0);
    ASSERT_NE(layer, nullptr);

    std::vector<uint32_t> hits;
    layer->query(0, 0, 1000000, 1000000, hits);
    EXPECT_EQ(hits.size(), 1u);

    hits.clear();
    layer->query(-1e12, -1e12, -1e11, -1e11, hits);
    EXPECT_TRUE(hits.empty());
}

TEST(DifLoader, QueryWithoutGrid) {
    cad_layer layer;
    auto poly = std::make_unique<cad_polygon>();
    poly->vertices = { {0,0}, {1,0}, {1,1}, {0,1} };
    poly->bbox_min_x = 0; poly->bbox_min_y = 0;
    poly->bbox_max_x = 1; poly->bbox_max_y = 1;
    layer.add_polygon(std::move(poly));

    std::vector<uint32_t> hits;
    layer.query(0, 0, 1, 1, hits);
    EXPECT_EQ(hits.size(), 1u);
}

TEST(DifLoader, BuildGridEmpty) {
    cad_layer layer;
    layer.build_grid(1000);
    EXPECT_TRUE(layer.grid.empty());
}

TEST(DifLoader, MissingFileThrows) {
    EXPECT_THROW(load_cad_file("Z:/missing_rle4k_unit.dif"), std::runtime_error);
    EXPECT_THROW(load_cad_file("no_extension"), std::runtime_error);
}

TEST(DifLoader, DocumentAccessors) {
    cad_document doc;
    EXPECT_EQ(doc.get_layer(0), nullptr);
    auto layer = std::make_unique<cad_layer>();
    layer->layer_id = 7;
    doc.add_layer(std::move(layer));
    EXPECT_EQ(doc.layer_count(), 1u);
    EXPECT_NE(doc.get_layer(0), nullptr);
}
