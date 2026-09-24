// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "coords.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace tilemap = corundum::world::tilemap;
using corundum::core::math::IntRect;
using tools::tilesmith::clamp_camera;
using tools::tilesmith::compute_palette_layout;
using tools::tilesmith::k_palette_cell_padding;
using tools::tilesmith::palette_click_to_gid;
using tools::tilesmith::PaletteCell;
using tools::tilesmith::pixel_to_tiled_rect;
using tools::tilesmith::snap_to_tile_rect;

namespace {

  tilemap::TilemapTileset make_tileset(tilemap::TileId first_gid, const std::vector<std::pair<int, int>> &sizes) {
    tilemap::TilemapTileset tileset;
    tileset.first_gid = first_gid;
    tileset.info.tile_count = static_cast<int>(sizes.size());
    for (const auto &[width, height] : sizes)
      tileset.info.tile_rects.push_back(IntRect{.x = 0, .y = 0, .width = width, .height = height});
    return tileset;
  }

} // namespace

TEST_CASE("compute_palette_layout — flows left-to-right and wraps to a new row") {
  const tilemap::TilemapTileset tileset = make_tileset(0, {{40, 10}, {40, 10}, {40, 10}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, /*available_w=*/100, /*tile_scale=*/1.f);

  REQUIRE(cells.size() == 3);
  CHECK(cells[0].local_id == 0);
  CHECK(cells[0].x == 0);
  CHECK(cells[0].y == 0);
  CHECK(cells[0].w == 40);
  CHECK(cells[0].h == 10);

  CHECK(cells[1].x == 40 + k_palette_cell_padding);
  CHECK(cells[1].y == 0);

  // Third tile no longer fits on row 0, so it wraps below the row's tallest cell.
  CHECK(cells[2].x == 0);
  CHECK(cells[2].y == 10 + k_palette_cell_padding);
}

TEST_CASE("compute_palette_layout — row breaks account for the tallest tile in the row") {
  const tilemap::TilemapTileset tileset = make_tileset(0, {{40, 30}, {40, 10}, {40, 5}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, 100, 1.f);

  REQUIRE(cells.size() == 3);
  CHECK(cells[1].y == 0);
  CHECK(cells[2].y == 30 + k_palette_cell_padding);
}

TEST_CASE("compute_palette_layout — scales tile sizes and clamps degenerate rects to one pixel") {
  const tilemap::TilemapTileset tileset = make_tileset(0, {{10, 5}, {0, 0}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, /*available_w=*/1000, /*tile_scale=*/2.f);

  REQUIRE(cells.size() == 2);
  CHECK(cells[0].w == 20);
  CHECK(cells[0].h == 10);
  CHECK(cells[1].w == 1);
  CHECK(cells[1].h == 1);
}

TEST_CASE("compute_palette_layout — an oversized tile still lays out at the row start") {
  const tilemap::TilemapTileset tileset = make_tileset(0, {{100, 10}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, /*available_w=*/50, /*tile_scale=*/1.f);

  REQUIRE(cells.size() == 1);
  CHECK(cells[0].x == 0);
  CHECK(cells[0].w == 100);
}

TEST_CASE("palette_click_to_gid — resolves the cell under the cursor to an absolute gid") {
  const tilemap::TilemapTileset tileset = make_tileset(/*first_gid=*/10, {{40, 10}, {40, 10}, {40, 10}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, 100, 1.f);

  const auto gid0 = palette_click_to_gid(5, 5, tileset, /*scroll_y=*/0.f, cells);
  REQUIRE(gid0.has_value());
  CHECK(*gid0 == 10);

  const auto gid1 = palette_click_to_gid(45, 5, tileset, 0.f, cells);
  REQUIRE(gid1.has_value());
  CHECK(*gid1 == 11);

  const auto gid2 = palette_click_to_gid(5, 13, tileset, 0.f, cells);
  REQUIRE(gid2.has_value());
  CHECK(*gid2 == 12);
}

TEST_CASE("palette_click_to_gid — scroll offset shifts the hit region") {
  const tilemap::TilemapTileset tileset = make_tileset(10, {{40, 10}, {40, 10}, {40, 10}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, 100, 1.f);

  // Row 2 starts at y=12; scrolled down 12px, a click at y=5 lands on it.
  const auto gid = palette_click_to_gid(5, 5, tileset, /*scroll_y=*/12.f, cells);
  REQUIRE(gid.has_value());
  CHECK(*gid == 12);
}

TEST_CASE("palette_click_to_gid — misses the padding gap between cells") {
  const tilemap::TilemapTileset tileset = make_tileset(10, {{40, 10}, {40, 10}});
  const std::vector<PaletteCell> cells = compute_palette_layout(tileset, 100, 1.f);

  CHECK_FALSE(palette_click_to_gid(/*panel_local_x=*/41, /*panel_local_y=*/5, tileset, 0.f, cells).has_value());
}

TEST_CASE("snap_to_tile_rect — a single tile produces a one-by-one rect") {
  const tilemap::CollisionRect rect = snap_to_tile_rect(/*col_a=*/2, /*row_a=*/3, /*col_b=*/2, /*row_b=*/3);
  CHECK(rect.col == 2.f);
  CHECK(rect.row == 3.f);
  CHECK(rect.col_span == 1.f);
  CHECK(rect.row_span == 1.f);
}

TEST_CASE("snap_to_tile_rect — normalizes a reversed drag to positive spans") {
  const tilemap::CollisionRect rect = snap_to_tile_rect(/*col_a=*/5, /*row_a=*/1, /*col_b=*/2, /*row_b=*/4);
  CHECK(rect.col == 2.f);
  CHECK(rect.row == 1.f);
  CHECK(rect.col_span == 4.f); // columns 2..5
  CHECK(rect.row_span == 4.f); // rows 1..4
}

TEST_CASE("clamp_camera — a map smaller than the canvas centers with negative offsets") {
  const auto [x, y] = clamp_camera(/*offset_x=*/100.f, /*offset_y=*/100.f, /*tile_scale=*/1.f, /*map_w=*/10,
                                   /*map_h=*/10, /*tw=*/64, /*diamond_h=*/32, /*canvas_w=*/1024, /*canvas_h=*/768);
  CHECK(x == doctest::Approx(0.f));
  CHECK(y == doctest::Approx(0.f));
}

TEST_CASE("clamp_camera — small maps allow negative offsets up to the centering limit") {
  const auto [x, y] = clamp_camera(-1000.f, -1000.f, 1.f, 10, 10, 64, 32, 1024, 768);
  CHECK(x == doctest::Approx(-160.f));
  CHECK(y == doctest::Approx(-208.f));

  const auto [inside_x, inside_y] = clamp_camera(-50.f, -50.f, 1.f, 10, 10, 64, 32, 1024, 768);
  CHECK(inside_x == doctest::Approx(-50.f));
  CHECK(inside_y == doctest::Approx(-50.f));
}

TEST_CASE("clamp_camera — large maps clamp to a non-negative scrollable range") {
  const auto [min_x, min_y] = clamp_camera(-100.f, -100.f, 1.f, 100, 100, 64, 32, 1024, 768);
  CHECK(min_x == doctest::Approx(0.f));
  CHECK(min_y == doctest::Approx(0.f));

  const auto [max_x, max_y] = clamp_camera(9999.f, 9999.f, 1.f, 100, 100, 64, 32, 1024, 768);
  CHECK(max_x == doctest::Approx(5440.f));
  CHECK(max_y == doctest::Approx(2464.f));
}

TEST_CASE("pixel_to_tiled_rect — a single-point drag collapses to the minimum tile span") {
  const tilemap::CollisionRect rect =
      pixel_to_tiled_rect(/*win_x_a=*/544, /*win_y_a=*/272, /*win_x_b=*/544, /*win_y_b=*/272, /*canvas_left=*/0,
                          /*canvas_top=*/0, /*canvas_w=*/1024, /*canvas_h=*/768, /*camera_x=*/0.f, /*camera_y=*/0.f,
                          /*tile_scale=*/1.f, /*elev_step=*/4.f, /*map_h=*/16, /*diamond_w=*/64, /*diamond_h=*/32);
  CHECK(rect.col_span == doctest::Approx(1.f / 64.f));
  CHECK(rect.row_span == doctest::Approx(1.f / 32.f));
}

TEST_CASE("pixel_to_tiled_rect — forward and reversed drags produce the same rect") {
  const tilemap::CollisionRect forward =
      pixel_to_tiled_rect(544, 272, 640, 352, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, 16, 64, 32);
  const tilemap::CollisionRect reversed =
      pixel_to_tiled_rect(640, 352, 544, 272, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, 16, 64, 32);

  CHECK(forward.col == doctest::Approx(reversed.col));
  CHECK(forward.row == doctest::Approx(reversed.row));
  CHECK(forward.col_span == doctest::Approx(reversed.col_span));
  CHECK(forward.row_span == doctest::Approx(reversed.row_span));
  CHECK(forward.col_span >= 1.f / 64.f);
  CHECK(forward.row_span >= 1.f / 32.f);
}
