#include <doctest/doctest.h>

#include "coords.hpp"
#include <corundum/core/math/vec.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

namespace tilemap = corundum::world::tilemap;
using corundum::core::math::Vec2;
using tools::tilesmith::pixel_to_fractional_tile;
using tools::tilesmith::screen_to_tile;

namespace {

  // 16x16 flat map with a single elevated tile at (0, 0). Defaults: tile_w=64, tile_h=32,
  // elev_step=4, k_content_margin=2.
  tilemap::Tilemap make_map_with_elevated_corner(uint8_t corner_elev) {
    tilemap::Tilemap tm;
    tm.width = 16;
    tm.height = 16;
    tm.iso_diamond_w = 64;
    tm.iso_diamond_h = 32;
    tilemap::TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(16 * 16), 1);
    layer.elevation.assign(static_cast<std::size_t>(16 * 16), 0);
    layer.elevation[0] = corner_elev; // (0, 0)
    tm.layers.push_back(std::move(layer));
    return tm;
  }

} // namespace

TEST_CASE("pixel_to_fractional_tile — flat map (all elev 0) matches current behavior") {
  // Sanity baseline: with no elevated cells, the picker returns the cell whose flat
  // projection is under the click — same as the buggy hardcoded-elev=0 path. No
  // regression for non-elevated terrain.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(0);
  const int tw = 64, dh = 32, map_h = 16;
  const int canvas_left = 0, canvas_top = 0, canvas_w = 1024, canvas_h = 768;
  const float tile_scale = 1.f, elev_step = 4.f;
  const float camera_x = 0.f, camera_y = 0.f;
  // Click at the center of cell (8, 8) — in-map so the picker has a valid result.
  // World position of (8, 8) center: x_origin=480 → ((8-8)*32+480, (8+8)*16) = (480, 256).
  // Origin shift: x = k_content_margin*half_tw = 2*32 = 64, y = half_th = 16.
  // Window px = 0 + 64 + 480 = 544, py = 0 + 16 + 256 = 272.
  const Vec2 frac = pixel_to_fractional_tile(/*px=*/544, /*py=*/272, canvas_left, canvas_top, canvas_w, canvas_h,
                                             camera_x, camera_y, tile_scale, elev_step, map_h, tw, dh, tm);
  CHECK(static_cast<int>(std::floor(frac.x)) == 8);
  CHECK(static_cast<int>(std::floor(frac.y)) == 8);
}

TEST_CASE("pixel_to_fractional_tile — elevated cell picks itself, not its flat-projected neighbor") {
  // The bug from plan §1f: cell (0, 0) at elev 5 renders 5*4 = 20 px above where its
  // flat projection would be. Clicking on the visible diamond should pick (0, 0).
  // With elev hardcoded to 0, world_to_tile with elev=0 gives the cell whose flat
  // projection is under the click — a different cell, off by one tile.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(5);
  const int tw = 64, dh = 32, map_h = 16;
  const int canvas_left = 0, canvas_top = 0, canvas_w = 1024, canvas_h = 768;
  const float tile_scale = 1.f, elev_step = 4.f;
  const float camera_x = 0.f, camera_y = 0.f;

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
  const Vec2 frac = pixel_to_fractional_tile(/*px=*/544, /*py=*/12, canvas_left, canvas_top, canvas_w, canvas_h,
                                             camera_x, camera_y, tile_scale, elev_step, map_h, tw, dh, tm);
  // The click is on the visible diamond of (0, 0) — its center in world space is
  // (480, -4) (elevated by 20 px above the flat projection at (480, 0)). The picker
  // accounts for the elevation lift and returns the diamond's center (0.5, 0.5),
  // which floors to cell (0, 0). With the old elev=0 hardcode, the click would map
  // to (-0.125, -0.125) — the cell whose flat projection is below the elevated diamond.
  CHECK(static_cast<int>(std::floor(frac.x)) == 0);
  CHECK(static_cast<int>(std::floor(frac.y)) == 0);
}

TEST_CASE("screen_to_tile — elevated cell returns correct tile, not flat-projected neighbor") {
  // Verifies the higher-level screen_to_tile wrapper also benefits from elev-aware picking.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(5);
  const std::optional<tools::tilesmith::TileCoord> tile =
      screen_to_tile(/*px=*/544, /*py=*/12, /*canvas_left=*/0, /*canvas_top=*/0,
                     /*canvas_w=*/1024, /*canvas_h=*/768, /*camera_x=*/0.f, /*camera_y=*/0.f, /*tile_scale=*/1.f,
                     /*elev_step=*/4.f, /*map_w=*/16, /*map_h=*/16, /*tw=*/64, /*dh=*/32, tm);
  REQUIRE(tile.has_value());
  CHECK(tile->col == 0);
  CHECK(tile->row == 0);
}

TEST_CASE("screen_to_tile — flat map baseline (no regression)") {
  // No elevated cells: behavior should be identical to the original elev=0 path.
  const tilemap::Tilemap tm = make_map_with_elevated_corner(0);
  // Click somewhere in the middle of the map (cell (8, 8)).
  const int tw = 64, dh = 32, map_w = 16, map_h = 16;
  const int x_origin = (map_h - 1) * (tw / 2); // 480
  // Cell (8, 8) center world position = ((8-8)*32 + 480, (8+8)*16) = (480, 256). Plus diamond half.
  const float world_y_center = 16.f * 16.f + 16.f;          // 256 + 16 = 272 (diamond center)
  const int px = 0 + 64 + x_origin;                         // 544
  const int py = 0 + 16 + static_cast<int>(world_y_center); // 288
  const std::optional<tools::tilesmith::TileCoord> tile =
      screen_to_tile(px, py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh, tm);
  REQUIRE(tile.has_value());
  CHECK(tile->col == 8);
  CHECK(tile->row == 8);
}

TEST_CASE(
    "screen_to_tile — flat overload picks wrong cell on raised diamond (regression for paint_or_erase_elevation)") {
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
  const int tw = 64, dh = 32, map_w = 16, map_h = 16;

  // Window-space click at the visible diamond center of raised cell (0, 0).
  // Same math as the elev-aware tests above:
  //   x_origin = (map_h - 1) * half_tw = 15 * 32 = 480
  //   top vertex of (0, 0) at elev 5: world = (0, -5*4) = (0, -20)
  //   diamond center: world = (480, -20 + 16) = (480, -4)
  //   origin_shift_x = k_content_margin * half_tw = 2 * 32 = 64
  //   origin_shift_y = half_th = 16
  //   px = canvas_left + 64 + 480 = 544
  //   py = canvas_top + 16 + (-4) = 12
  constexpr int px = 544;
  constexpr int py = 12;

  // Elevation-aware overload: picks the raised cell.
  const std::optional<tools::tilesmith::TileCoord> elev_tile =
      screen_to_tile(px, py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh, tm);
  REQUIRE(elev_tile.has_value());
  CHECK(elev_tile->col == 0);
  CHECK(elev_tile->row == 0);

  // Flat overload: must NOT pick (0, 0) — that's the whole bug. We don't pin
  // down exactly which off-by-one cell it picks (depends on elev/elev_step
  // arithmetic details); the relevant property is "wrong, not (0, 0)".
  const std::optional<tools::tilesmith::TileCoord> flat_tile =
      screen_to_tile(px, py, 0, 0, 1024, 768, 0.f, 0.f, 1.f, 4.f, map_w, map_h, tw, dh);
  if (flat_tile.has_value()) {
    CHECK((flat_tile->col != elev_tile->col || flat_tile->row != elev_tile->row));
  }
}
