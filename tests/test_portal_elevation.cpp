#include <doctest/doctest.h>

#include <corundum/physics/physics_sys.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

namespace tilemap = corundum::world::tilemap;
using corundum::physics::portal_elev_matches;
using corundum::world::MapView;
using corundum::world::Portal;
using corundum::world::tilemap::Tilemap;
using corundum::world::tilemap::TilemapLayer;

namespace {

  Tilemap make_flat_map(int w, int h, uint8_t elev) {
    Tilemap tm;
    tm.width = w;
    tm.height = h;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(w * h), 1);
    layer.elevation.assign(static_cast<std::size_t>(w * h), elev);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  MapView map_with_elevation_map(const Tilemap &tm) {
    MapView map;
    map.elevation_map = &tm;
    return map;
  }

} // namespace

TEST_CASE("portal_elev_matches — player at portal elevation triggers") {
  // Portal cell at elev 0; player at elev 0 with tolerance 0 → match.
  const Tilemap tm = make_flat_map(4, 4, 0);
  const MapView map = map_with_elevation_map(tm);
  const Portal p{0.f, 0.f, 1.f, 1.f, "", 0, 0}; // 1x1 portal at (0,0) center (0.5, 0.5)
  CHECK(portal_elev_matches(map, p, /*player_elev=*/0, /*tolerance=*/0));
}

TEST_CASE("portal_elev_matches — adjacent cells at different elev reject trigger") {
  // Player on cell (0, 0) at elev 5 (e.g., on a bridge); portal authored on cell (1, 0)
  // at elev 0 (ground floor below). Without elevation gating the player teleports
  // between floors whenever they cross the portal — the bug from plan §4a. With the fix,
  // the elev mismatch rejects the trigger.
  Tilemap tm;
  tm.width = 4;
  tm.height = 4;
  TilemapLayer layer;
  layer.name = "ground";
  layer.z_index = 0;
  layer.visible = true;
  layer.tiles.assign(static_cast<std::size_t>(4 * 4), 1);
  layer.elevation.assign(static_cast<std::size_t>(4 * 4), 0);
  layer.elevation[0 * 4 + 0] = 5; // (0, 0) is the bridge cell — player stands here
  tm.layers.push_back(std::move(layer));
  const MapView map = map_with_elevation_map(tm);

  const Portal p{1.f, 0.f, 1.f, 1.f, "", 0, 0}; // portal at (1, 0), elev 0
  // Player_elev = 5 (on the bridge cell), portal_elev = 0 (ground portal). |0 - 5| = 5 > 0.
  CHECK_FALSE(portal_elev_matches(map, p, /*player_elev=*/5, /*tolerance=*/0));
}

TEST_CASE("portal_elev_matches — tolerance widens the gate (ramp-aware)") {
  // Player crossing a ramp midpoint has rounded elev 2; portal at top elev 4; tolerance 2
  // (the ramp's ceil(Δ/2)) makes the top portal still within reach.
  Tilemap tm;
  tm.width = 4;
  tm.height = 4;
  TilemapLayer layer;
  layer.name = "ground";
  layer.z_index = 0;
  layer.visible = true;
  layer.tiles.assign(static_cast<std::size_t>(4 * 4), 1);
  layer.elevation.assign(static_cast<std::size_t>(4 * 4), 4); // all cells elev 4
  tm.layers.push_back(std::move(layer));
  const MapView map = map_with_elevation_map(tm);

  const Portal p{0.f, 0.f, 1.f, 1.f, "", 0, 0}; // cell (0,0) at elev 4
  // Player elev 2, tolerance 0 → no match (|4-2|=2 > 0).
  CHECK_FALSE(portal_elev_matches(map, p, /*player_elev=*/2, /*tolerance=*/0));
  // Player elev 2, tolerance 2 → match (|4-2|=2 ≤ 2).
  CHECK(portal_elev_matches(map, p, /*player_elev=*/2, /*tolerance=*/2));
}
