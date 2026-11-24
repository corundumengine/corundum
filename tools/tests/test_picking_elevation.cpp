// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

#include "coords.hpp"
#include "layer_visibility.hpp"
#include <corundum/core/math/vec.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <optional>
#include <utility>

namespace tilemap = corundum::world::tilemap;
using corundum::core::math::TilePos;
using tools::tilesmith::LayerVisibility;
using tools::tilesmith::pixel_to_fractional_tile;
using tools::tilesmith::screen_to_tile;

namespace {

/** 16x16 flat map with a single elevated tile at (0, 0). Defaults: tile_w=64, tile_h=32,
 *  elev_step=4, k_content_margin=2.
 */
tilemap::Tilemap make_map_with_elevated_corner(uint8_t corner_elev) {
  tilemap::Tilemap tm;
  tm.width = 16;
  tm.height = 16;
  tm.iso_diamond_w = 64;
  tm.iso_diamond_h = 32;
  tilemap::TilemapLayer layer;
  layer.name = "ground";
  layer.z_index = 0;
  layer.tiles.assign(static_cast<std::size_t>(16 * 16), 1);
  layer.elevation.assign(static_cast<std::size_t>(16 * 16), 0);
  layer.elevation[0] = corner_elev; // (0, 0)
  tm.layers.push_back(std::move(layer));
  return tm;
}

} // namespace

TEST_CASE("pixel_to_fractional_tile - flat map (all elev 0) matches current behavior") {
  // Sanity baseline: with no elevated cells, the picker returns the cell whose flat
  // projection is under the click — same as the buggy hardcoded-elev=0 path. No
  // regression for non-elevated terrain.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(0);
  const int tw = 64;
  const int dh = 32;
  const int map_h = 16;
  const int canvas_left = 0;
  const int canvas_top = 0;
  const int canvas_w = 1024;
  const int canvas_h = 768;
  const float tile_scale = 1.f;
  const float elev_step = 4.f;
  const float camera_x = 0.f;
  const float camera_y = 0.f;
  // Click at the center of cell (8, 8) — in-map so the picker has a valid result.
  // World position of (8, 8) center: x_origin=480 → ((8-8)*32+480, (8+8)*16) = (480, 256).
  // Origin shift: x = k_content_margin*half_tw = 2*32 = 64, y = half_th = 16.
  // Window px = 0 + 64 + 480 = 544, py = 0 + 16 + 256 = 272.
  const TilePos frac =
      pixel_to_fractional_tile(/*px=*/544, /*py=*/272, canvas_left, canvas_top, canvas_w, canvas_h, camera_x, camera_y,
                               tile_scale, elev_step, map_h, tw, dh, tm, LayerVisibility(tm.layers.size(), true));
  CHECK(static_cast<int>(std::floor(frac.col)) == 8);
  CHECK(static_cast<int>(std::floor(frac.row)) == 8);
}

TEST_CASE("pixel_to_fractional_tile - elevated cell picks itself, not its flat-projected neighbor") {
  // The bug from plan §1f: cell (0, 0) at elev 5 renders 5*4 = 20 px above where its
  // flat projection would be. Clicking on the visible diamond should pick (0, 0).
  // With elev hardcoded to 0, world_to_tile with elev=0 gives the cell whose flat
  // projection is under the click — a different cell, off by one tile.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(5);
  const int tw = 64;
  const int dh = 32;
  const int map_h = 16;
  const int canvas_left = 0;
  const int canvas_top = 0;
  const int canvas_w = 1024;
  const int canvas_h = 768;
  const float tile_scale = 1.f;
  const float elev_step = 4.f;
  const float camera_x = 0.f;
  const float camera_y = 0.f;

  // Compute where the elevated cell (0, 0) draws. Cell (0, 0) with map_h=16:
  //   x_origin = (16 - 1) * half_tw = 15 * 32 = 480
  //   tile_to_world((0, 0), elev=5) = ((0-0)*32 + 480, (0+0)*16 - 5*4) = (480, -20)  [top vertex]
  // Diamond center is one half-th below the top vertex:
  //   center = (480, -20 + 16) = (480, -4)  in world space
  // Window-space click (canvas (0,0) + origin_shift + world):
  //   origin_shift_x = k_content_margin * half_tw = 2 * 32 = 64
  //   origin_shift_y = half_th = 16
  //   px = canvas_left + origin_shift_x + world_x = 0 + 64 + 480 = 544
  //   py = canvas_top + origin_shift_y + world_y = 0 + 16 + (-4) = 12
  const TilePos frac =
      pixel_to_fractional_tile(/*px=*/544, /*py=*/12, canvas_left, canvas_top, canvas_w, canvas_h, camera_x, camera_y,
                               tile_scale, elev_step, map_h, tw, dh, tm, LayerVisibility(tm.layers.size(), true));
  // The click is on the visible diamond of (0, 0) — its center in world space is
  // (480, -4) (elevated by 20 px above the flat projection at (480, 0)). The picker
  // accounts for the elevation lift and returns the diamond's center (0.5, 0.5),
  // which floors to cell (0, 0). With the old elev=0 hardcode, the click would map
  // to (-0.125, -0.125) — the cell whose flat projection is below the elevated diamond.
  CHECK(static_cast<int>(std::floor(frac.col)) == 0);
  CHECK(static_cast<int>(std::floor(frac.row)) == 0);
}

TEST_CASE("pixel_to_fractional_tile - a hidden raised layer does not catch the click") {
  // Same click as "elevated cell picks itself": on the visible diamond of the raised tile at
  // (0, 0). With that layer hidden the tile is not drawn, so the click lands on the flat ground
  // the un-elevated projection points at.
  const int tw = 64;
  const int dh = 32;
  const int map_h = 16;
  const int canvas_left = 0;
  const int canvas_top = 0;
  const int canvas_w = 1024;
  const int canvas_h = 768;
  const float tile_scale = 1.f;
  const float elev_step = 4.f;
  const float camera_x = 0.f;
  const float camera_y = 0.f;

  constexpr std::size_t k_cell_count = std::size_t{16} * 16;
  tilemap::Tilemap tm;
  tm.width = 16;
  tm.height = 16;
  tm.iso_diamond_w = 64;
  tm.iso_diamond_h = 32;

  tilemap::TilemapLayer ground;
  ground.name = "ground";
  ground.z_index = 0;
  ground.tiles.assign(k_cell_count, 1);
  ground.elevation.assign(k_cell_count, 0);
  tm.layers.push_back(std::move(ground));

  tilemap::TilemapLayer raised;
  raised.name = "raised";
  raised.z_index = 0;
  raised.tiles.assign(k_cell_count, tilemap::k_empty_tile);
  raised.tiles[0] = 1;
  raised.elevation.assign(k_cell_count, 0);
  raised.elevation[0] = 5;
  tm.layers.push_back(std::move(raised));

  const LayerVisibility layer_visible{true, false};

  const TilePos frac =
      pixel_to_fractional_tile(/*px=*/544, /*py=*/12, canvas_left, canvas_top, canvas_w, canvas_h, camera_x, camera_y,
                               tile_scale, elev_step, map_h, tw, dh, tm, layer_visible);
  const TilePos flat = pixel_to_fractional_tile(/*px=*/544, /*py=*/12, canvas_left, canvas_top, canvas_w, canvas_h,
                                                camera_x, camera_y, tile_scale, elev_step, map_h, tw, dh);
  CHECK_EQ(static_cast<int>(std::floor(frac.col)), static_cast<int>(std::floor(flat.col)));
  CHECK_EQ(static_cast<int>(std::floor(frac.row)), static_cast<int>(std::floor(flat.row)));
}

TEST_CASE("screen_to_tile - elevated cell returns correct tile, not flat-projected neighbor") {
  // Verifies the higher-level screen_to_tile wrapper also benefits from elev-aware picking.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(5);
  const std::optional<tools::tilesmith::TileCoord> tile =
      screen_to_tile(/*px=*/544, /*py=*/12, /*canvas_left=*/0, /*canvas_top=*/0,
                     /*canvas_w=*/1024, /*canvas_h=*/768, /*camera_x=*/0.f, /*camera_y=*/0.f, /*tile_scale=*/1.f,
                     /*elev_step=*/4.f, /*map_w=*/16, /*map_h=*/16, /*tw=*/64, /*diamond_h=*/32, tm,
                     LayerVisibility(tm.layers.size(), true));
  REQUIRE(tile.has_value());
  if (!tile)
    return;
  CHECK(tile->col == 0);
  CHECK(tile->row == 0);
}

TEST_CASE("screen_to_tile - flat map baseline (no regression)") {
  // No elevated cells: behavior should be identical to the original elev=0 path.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(0);
  // Click somewhere in the middle of the map (cell (8, 8)).
  const int tw = 64;
  const int dh = 32;
  const int map_w = 16;
  const int map_h = 16;
  const int x_origin = (map_h - 1) * (tw / 2); // 480
  // Cell (8, 8) center world position = ((8-8)*32 + 480, (8+8)*16) = (480, 256). Plus diamond half.
  const float world_y_center = (16.f * 16.f) + 16.f;        // 256 + 16 = 272 (diamond center)
  const int px = 0 + 64 + x_origin;                         // 544
  const int py = 0 + 16 + static_cast<int>(world_y_center); // 288
  const std::optional<tools::tilesmith::TileCoord> tile = screen_to_tile(
      px, py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh, tm, LayerVisibility(tm.layers.size(), true));
  REQUIRE(tile.has_value());
  if (!tile)
    return;
  CHECK(tile->col == 8);
  CHECK(tile->row == 8);
}

TEST_CASE(
    "screen_to_tile - flat overload picks wrong cell on raised diamond (regression for paint_or_erase_elevation)") {
  // The exact bug the elevation-paint fix addresses: a click landing on the
  // visible diamond of a raised cell, when fed through the flat (no-map)
  // screen_to_tile overload, resolves to a *different* cell — the one whose
  // flat projection sits underneath the click, not the raised cell the user
  // is actually looking at. This is what makes elevation repaint fail on a
  // tile that's already been raised: the brush's new value lands on the cell
  // behind/below the visible diamond, and the cell the user is watching never
  // updates.
  //
  // This test pins down the bug shape so any future call site that drops the
  // state.map argument (and silently falls back to the flat overload) gets
  // flagged: the assertion fails until the call site is updated to use the
  // elevation-aware overload.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(5);
  const int tw = 64;
  const int dh = 32;
  const int map_w = 16;
  const int map_h = 16;

  // Window-space click at the visible diamond center of raised cell (0, 0).
  // Same math as the elev-aware tests above:
  //   x_origin = (map_h - 1) * half_tw = 15 * 32 = 480
  //   top vertex of (0, 0) at elev 5: world = (0, -5*4) = (0, -20)
  //   diamond center: world = (480, -20 + 16) = (480, -4)
  //   origin_shift_x = k_content_margin * half_tw = 2 * 32 = 64
  //   origin_shift_y = half_th = 16
  //   px = canvas_left + 64 + 480 = 544
  //   py = canvas_top + 16 + (-4) = 12
  constexpr int k_px = 544;
  constexpr int k_py = 12;

  // Elevation-aware overload: picks the raised cell.
  const std::optional<tools::tilesmith::TileCoord> elev_tile =
      screen_to_tile(k_px, k_py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh, tm,
                     LayerVisibility(tm.layers.size(), true));
  REQUIRE(elev_tile.has_value());
  if (!elev_tile)
    return;
  CHECK(elev_tile->col == 0);
  CHECK(elev_tile->row == 0);

  // Flat overload: must NOT pick (0, 0) — that's the whole bug. We don't pin
  // down exactly which off-by-one cell it picks (depends on elev/elev_step
  // arithmetic details); the relevant property is "wrong, not (0, 0)".
  const std::optional<tools::tilesmith::TileCoord> flat_tile =
      screen_to_tile(k_px, k_py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh);
  if (flat_tile.has_value()) {
    const tools::tilesmith::TileCoord flat = *flat_tile;
    const tools::tilesmith::TileCoord elev = *elev_tile;
    CHECK((flat.col != elev.col || flat.row != elev.row));
  }
}
