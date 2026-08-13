#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "base/shared.h"
#include "cndefs.h"

BEGIN_CAM_NAMESPACE()

struct cad_vertex { double x = 0, y = 0; cad_vertex() = default; cad_vertex(double a, double b) : x(a), y(b) {} };

struct cad_polygon {
    std::vector<cad_vertex> vertices;
    bool polarity = true;
    /* bounding box in document coordinates (nm) --- from DIF header */
    double bbox_min_x = 0, bbox_min_y = 0, bbox_max_x = 0, bbox_max_y = 0;
    size_t vertex_count() const { return vertices.size(); }
};

struct cad_layer {
    int layer_id = 0;
    std::string layer_name;
    std::vector<std::unique_ptr<cad_polygon>> polygons;
    bool polarity = true;
    size_t polygon_count() const { return polygons.size(); }
    void add_polygon(std::unique_ptr<cad_polygon> p) { polygons.push_back(std::move(p)); }

    /* ---- spatial grid index (built after loading) ---- */
    struct grid_index {
        double origin_x = 0, origin_y = 0;
        double cell_w = 0, cell_h = 0;
        int cols = 0, rows = 0;
        std::vector<std::vector<uint32_t>> cells;  /* cells[r*cols+c] → polygon indices */
        bool empty() const { return cells.empty(); }
    };
    grid_index grid;

    /* query: return polygon indices whose bbox intersects [x0,y0]-[x1,y1] */
    void query(double x0, double y0, double x1, double y1,
               std::vector<uint32_t>& out) const;
    void build_grid(double cell_size_nm);
};

struct cad_document {
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    uint32_t image_width = 0, image_height = 0;
    double pixel_size_nm = 10000.0;
    std::vector<std::unique_ptr<cad_layer>> layers;
    size_t layer_count() const { return layers.size(); }
    size_t total_polygon_count() const {
        size_t n = 0; for (auto& l : layers) n += l->polygon_count(); return n;
    }
    const cad_layer* get_layer(size_t i) const { return i < layers.size() ? layers[i].get() : nullptr; }
    void add_layer(std::unique_ptr<cad_layer> l) { layers.push_back(std::move(l)); }
};

enum class cad_file_type { unknown, dif };
cad_file_type detect_file_type(const std::string& path);

class cad_loader {
public:
    virtual ~cad_loader() = default;
    virtual std::unique_ptr<cad_document> load(const std::string& path) = 0;
    virtual cad_file_type get_type() const = 0;
};

std::unique_ptr<cad_loader> create_cad_loader(cad_file_type type);
std::unique_ptr<cad_document> load_cad_file(const std::string& path);

END_CAM_NAMESPACE()

BEGIN_CAM_NAMESPACE()
class rle4k_dif_loader : public cad_loader {
public:
    std::unique_ptr<cad_document> load(const std::string& path) override;
    cad_file_type get_type() const override { return cad_file_type::dif; }
};
END_CAM_NAMESPACE()
