#pragma once
/*
 * rasterizer --- AET scanline rasterizer for CAD polygons.
 *
 * Processes one polygon at a time with a small edge table, only walking
 * the polygon's own Y range.
 */

#include <memory>
#include <cstdint>

#include "base/shared.h"
#include <dif_loader.h>
#include <bitmap_info.h>

BEGIN_SP2_NAMESPACE()

/*
 * Rasterize a single block (col, block_row) from the CAD document
 * into a 1-bit-per-pixel bitmap.
 *
 * @param doc       CAD document with layers/polygons
 * @param col       block column index (0-based)
 * @param block_row block row index (0-based)
 * @param pw_nm     pixel pitch in nanometers
 * @param sw        strip width in pixels
 * @param sh        strip height (block line) in pixels
 * @param align     bitmap alignment mode
 * @return          bitmap_info or nullptr if block is out of bounds
 */
std::unique_ptr<bitmap_info> rle4k_rasterize_block(
    const cam::cad_document* doc,
    int col, int block_row,
    double pw_nm, uint32_t sw, uint32_t sh,
    map_align_mode align);

END_SP2_NAMESPACE()
