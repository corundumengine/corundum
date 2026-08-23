#include <doctest/doctest.h>

#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/sys/physics_sys.hpp>

namespace tilemap = corundum::gameplay::world::tilemap;
using corundum::gameplay::world::MapView;
using corundum::gameplay::world::tilemap::RampAxis;
using corundum::gameplay::world::tilemap::Tilemap;
using corundum::gameplay::world::tilemap::TilemapLayer;
using corundum::physics::sys::compute_elevation_gate;
using corundum::physics::sys::ElevationGate;

namespace {

  // Build a single-tile-per-cell tilemap with a per-cell elevation vector. Ramps
  // are optional; ramp_cell is the (col, row) of the ramp and ramp_axis is its axis.
  Tilemap make_map(int w, int h, const std::vector<uint8_t> &elevs,
                   std::optional<std::pair<int, int>> ramp_cell = std::nullopt,
                   RampAxis ramp_axis = RampAxis::NorthSouth) {
    Tilemap tm;
    tm.width = w;
    tm.height = h;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(w * h), 1);
    layer.elevation = elevs;
    if (ramp_cell) {
      const auto [rc, rr] = *ramp_cell;
      layer.ramps[rr * w + rc] = ramp_axis;
    }
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  MapView map_with_elevation_map(const Tilemap &tm) {
    MapView map;
    map.elevation_map = &tm;
    return map;
  }

} // namespace

TEST_CASE("compute_elevation_gate — World mode (no elevation_map) returns 0/0") {
  // elevation_map null (chunked/streamed World mode). Helper shouldn't dereference null.
  MapView map;
  const ElevationGate g = compute_elevation_gate(map, 5.5f, 5.5f);
  CHECK(g.player_elevation == 0);
  CHECK(g.tolerance == 0);
}

TEST_CASE("compute_elevation_gate — flat terrain: round elev + zero tolerance") {
  // All cells elev 5, no ramps. Player at (1.5, 1.5): interpolated elev = 5.
  std::vector<uint8_t> elevs(16, 5);
  const Tilemap tm = make_map(4, 4, elevs);
  const MapView map = map_with_elevation_map(tm);

  const ElevationGate g = compute_elevation_gate(map, 1.5f, 1.5f);
  CHECK(g.player_elevation == 5);
  CHECK(g.tolerance == 0);
}

TEST_CASE("compute_elevation_gate — round-to-nearest elev (not truncate)") {
  // Reproduces the truncation bug: at fractional ramp position, interpolated elev
  // passes through 0.99, 1.99, 2.99, 3.99. Old (truncate) gates the player at 0/1/2/3
  // so colliders at the top elevation (4) don't block until the ramp is fully crossed.
  // New (round) gates correctly at the nearest integer.
  std::vector<uint8_t> elevs(16, 0);
  // Ramp from elev 0 (north) to elev 4 (south) on cell (1, 1), neighbors (1, 0)=0, (1, 2)=4.
  elevs[0 * 4 + 1] = 0;
  elevs[1 * 4 + 1] = 0; // ramp cell's own integer elev doesn't matter (interpolated from neighbors)
  elevs[2 * 4 + 1] = 4;
  const Tilemap tm = make_map(4, 4, elevs, std::pair{1, 1}, RampAxis::NorthSouth);
  const MapView map = map_with_elevation_map(tm);

  // At (1.5, 1.99): t = 0.99, interpolated elev = lerp(0, 4, 0.99) = 3.96.
  // Old (truncate): player_elev = 3. New (round): player_elev = 4.
  const ElevationGate g = compute_elevation_gate(map, 1.5f, 1.99f);
  CHECK(g.player_elevation == 4);
}

TEST_CASE("compute_elevation_gate — ramp tolerance = ceil(Δ/2) widens to both ends") {
  // The other half of the bug: at ramp midpoint, player is gated at elev 2 (rounded).
  // With tolerance = 0, colliders at BOTH elev 0 and elev 4 are filtered out (|0-2|=2>0,
  // |4-2|=2>0) — walls at both ends become ghosts mid-ramp. Widening tolerance to
  // ceil(4/2)=2 makes both ends within tolerance.
  std::vector<uint8_t> elevs(16, 0);
  elevs[0 * 4 + 1] = 0;
  elevs[1 * 4 + 1] = 0;
  elevs[2 * 4 + 1] = 4;
  const Tilemap tm = make_map(4, 4, elevs, std::pair{1, 1}, RampAxis::NorthSouth);
  const MapView map = map_with_elevation_map(tm);

  // Player at ramp midpoint (1.5, 1.5): interpolated elev = 2, tolerance = 2.
  const ElevationGate g = compute_elevation_gate(map, 1.5f, 1.5f);
  CHECK(g.player_elevation == 2);
  CHECK(g.tolerance == 2);

  // Off-ramp player (in cell (1, 0)): no ramp, tolerance = 0.
  const ElevationGate g2 = compute_elevation_gate(map, 1.5f, 0.5f);
  CHECK(g2.player_elevation == 0);
  CHECK(g2.tolerance == 0);
}

TEST_CASE("compute_elevation_gate — odd ramp Δ uses ceil(Δ/2), not floor") {
  // Ramp from 0 to 3: ceil(3/2) = 2, not 1. Player at midpoint (interpolated 1.5)
  // rounded to 2 should see both ends' colliders.
  std::vector<uint8_t> elevs(16, 0);
  elevs[0 * 4 + 1] = 0;
  elevs[1 * 4 + 1] = 0;
  elevs[2 * 4 + 1] = 3;
  const Tilemap tm = make_map(4, 4, elevs, std::pair{1, 1}, RampAxis::NorthSouth);
  const MapView map = map_with_elevation_map(tm);

  const ElevationGate g = compute_elevation_gate(map, 1.5f, 1.5f);
  CHECK(g.tolerance == 2);
}

TEST_CASE("compute_elevation_gate — EAST_WEST ramp axis") {
  // Ramp from (0, 1) elev 0 to (2, 1) elev 4, axis EAST_WEST, ramp cell at (1, 1).
  std::vector<uint8_t> elevs(16, 0);
  elevs[1 * 4 + 0] = 0;
  elevs[1 * 4 + 1] = 0;
  elevs[1 * 4 + 2] = 4;
  const Tilemap tm = make_map(4, 4, elevs, std::pair{1, 1}, RampAxis::EastWest);
  const MapView map = map_with_elevation_map(tm);

  // Player at ramp cell col center (1.5, 1.5): interpolated elev = lerp(0, 4, 0.5) = 2.
  const ElevationGate g = compute_elevation_gate(map, 1.5f, 1.5f);
  CHECK(g.player_elevation == 2);
  CHECK(g.tolerance == 2);
}
