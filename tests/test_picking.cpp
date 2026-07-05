#include <doctest/doctest.h>

#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>

using corundum::gameplay::sys::pick_tile;
using corundum::gameplay::world::Camera;
using corundum::gameplay::world::MapView;
using corundum::gameplay::world::tilemap::Tilemap;
using corundum::gameplay::world::tilemap::TilemapLayer;

namespace {

  constexpr float k_half_tw = 32.f;
  constexpr float k_half_th = 16.f;
  constexpr float k_elev_step = 4.f;

  // 2x2 flat map, all tiles present so elevation_at() registers data at every cell.
  Tilemap make_flat_map() {
    Tilemap tm;
    tm.width = 2;
    tm.height = 2;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(tm.width * tm.height), 1);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  // Same 2x2 map, but (1,1) is raised — chosen (elevation 8, elev_step 4) so its
  // raised diamond's center inverts, at elevation 0, to exactly cell (0,0)'s center —
  // i.e. (0,0) and (1,1) both claim the same screen point, testing topmost-first
  // resolution. See plan derivation: center of (1,1) raised by 8 is world (0,16);
  // inverting (0,16) at elevation 0 gives fractional (0.5,0.5) -> floor (0,0).
  Tilemap make_overlap_map() {
    Tilemap tm = make_flat_map();
    tm.layers[0].elevation.assign(static_cast<std::size_t>(tm.width * tm.height), 0);
    tm.layers[0].elevation[1 * tm.width + 1] = 8;
    return tm;
  }

  MapView make_view(const Tilemap &tm) {
    MapView v;
    v.half_tw = k_half_tw;
    v.half_th = k_half_th;
    v.x_origin = 0.f;
    v.elevation_map = &tm;
    return v;
  }

} // namespace

TEST_CASE("pick_tile — flat map resolves the tile under the cursor") {
  const Tilemap tm = make_flat_map();
  const MapView map = make_view(tm);
  const Camera camera{0.f, 0.f};

  // Center of flat tile (0,0) is world (0, 16) — see derivation above.
  const auto result = pick_tile(0.f, 16.f, camera, map, k_elev_step, 1.f);
  REQUIRE(result.has_value());
  CHECK(result->col == 0);
  CHECK(result->row == 0);
}

TEST_CASE("pick_tile — applies camera offset before inverting") {
  const Tilemap tm = make_flat_map();
  const MapView map = make_view(tm);
  // Shift the camera so the same tile is now hit from a different window-space position.
  const Camera camera{10.f, -5.f};
  const auto result = pick_tile(0.f - 10.f, 16.f + 5.f, camera, map, k_elev_step, 1.f);
  REQUIRE(result.has_value());
  CHECK(result->col == 0);
  CHECK(result->row == 0);
}

TEST_CASE("pick_tile — topmost-first: a raised tile wins over the lower cell it overhangs") {
  const Tilemap tm = make_overlap_map();
  const MapView map = make_view(tm);
  const Camera camera{0.f, 0.f};

  // This screen point is claimed by both (0,0) [flat, real elevation 0] and (1,1)
  // [raised to 8, whose diamond visually overhangs this point]. The raised tile has
  // the larger iso_depth_key and must win.
  const auto result = pick_tile(0.f, 16.f, camera, map, k_elev_step, 1.f);
  REQUIRE(result.has_value());
  CHECK(result->col == 1);
  CHECK(result->row == 1);
}

TEST_CASE("pick_tile — out of bounds returns nullopt") {
  const Tilemap tm = make_flat_map();
  const MapView map = make_view(tm);
  const Camera camera{0.f, 0.f};
  const auto result = pick_tile(10000.f, 10000.f, camera, map, k_elev_step, 1.f);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("pick_tile — zoom scales the screen-to-world conversion") {
  const Tilemap tm = make_flat_map();
  const MapView map = make_view(tm);
  const Camera camera{0.f, 0.f};

  // At 2x zoom, screen point (0, 32) maps to world (0, 16) — the same point that
  // (0, 16) maps to at zoom 1 — since world = screen / zoom + camera.
  const auto result = pick_tile(0.f, 32.f, camera, map, k_elev_step, 2.f);
  REQUIRE(result.has_value());
  CHECK(result->col == 0);
  CHECK(result->row == 0);
}

TEST_CASE("pick_tile — null elevation_map (World mode) returns nullopt") {
  MapView map;
  map.half_tw = k_half_tw;
  map.half_th = k_half_th;
  map.elevation_map = nullptr;
  const Camera camera{0.f, 0.f};
  const auto result = pick_tile(0.f, 16.f, camera, map, k_elev_step, 1.f);
  CHECK_FALSE(result.has_value());
}
