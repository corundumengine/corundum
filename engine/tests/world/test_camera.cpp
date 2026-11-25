// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/camera.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/world_bounds.hpp>

using corundum::world::Camera;
using corundum::world::MapView;
using corundum::world::WorldBounds;

namespace {

constexpr float k_min_zoom{0.5f};
constexpr float k_max_zoom{3.f};

/** World-space point currently under (anchor_x, anchor_y) at the camera's current zoom. */
float world_under_anchor(float camera_pos, float anchor, float zoom) {
  return camera_pos + (anchor / zoom);
}

} // namespace

TEST_CASE("apply_zoom - no-op when zoom_delta is zero") {
  Camera camera{.x = 10.f, .y = 20.f, .zoom = 1.5f};
  camera.apply_zoom(0.f, 100.f, 50.f, k_min_zoom, k_max_zoom);
  CHECK(camera.x == doctest::Approx(10.f));
  CHECK(camera.y == doctest::Approx(20.f));
  CHECK(camera.zoom == doctest::Approx(1.5f));
}

TEST_CASE("apply_zoom - keeps the mouse-cursor anchor's world point fixed") {
  Camera camera{.x = 5.f, .y = -3.f, .zoom = 1.f};
  constexpr float k_anchor_x{400.f};
  constexpr float k_anchor_y{250.f};

  const float world_x_before{world_under_anchor(camera.x, k_anchor_x, camera.zoom)};
  const float world_y_before{world_under_anchor(camera.y, k_anchor_y, camera.zoom)};

  camera.apply_zoom(1.f, k_anchor_x, k_anchor_y, k_min_zoom, k_max_zoom);

  CHECK(camera.zoom > 1.f);
  const float world_x_after{world_under_anchor(camera.x, k_anchor_x, camera.zoom)};
  const float world_y_after{world_under_anchor(camera.y, k_anchor_y, camera.zoom)};
  CHECK(world_x_after == doctest::Approx(world_x_before));
  CHECK(world_y_after == doctest::Approx(world_y_before));
}

TEST_CASE("apply_zoom - keeps the screen-center anchor's world point fixed on zoom out") {
  Camera camera{.x = 100.f, .y = 60.f, .zoom = 2.f};
  constexpr float k_anchor_x{960.f};
  constexpr float k_anchor_y{540.f};

  const float world_x_before{world_under_anchor(camera.x, k_anchor_x, camera.zoom)};
  const float world_y_before{world_under_anchor(camera.y, k_anchor_y, camera.zoom)};

  camera.apply_zoom(-1.f, k_anchor_x, k_anchor_y, k_min_zoom, k_max_zoom);

  CHECK(camera.zoom < 2.f);
  const float world_x_after{world_under_anchor(camera.x, k_anchor_x, camera.zoom)};
  const float world_y_after{world_under_anchor(camera.y, k_anchor_y, camera.zoom)};
  CHECK(world_x_after == doctest::Approx(world_x_before));
  CHECK(world_y_after == doctest::Approx(world_y_before));
}

TEST_CASE("apply_zoom - clamps to max_zoom") {
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 2.9f};
  camera.apply_zoom(5.f, 0.f, 0.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_max_zoom));
}

TEST_CASE("apply_zoom - clamps to min_zoom") {
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 0.55f};
  camera.apply_zoom(-5.f, 0.f, 0.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_min_zoom));
}

TEST_CASE("apply_zoom - already-clamped zoom stays put and camera position is untouched") {
  Camera camera{.x = 7.f, .y = 9.f, .zoom = k_max_zoom};
  camera.apply_zoom(1.f, 100.f, 100.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_max_zoom));
  CHECK(camera.x == doctest::Approx(7.f));
  CHECK(camera.y == doctest::Approx(9.f));
}

TEST_CASE("follow_player - zoomed-in viewport clamps to a smaller effective world extent") {
  MapView map;
  map.world_bounds = {.width_px = 2000.f, .height_px = 2000.f};
  constexpr float k_win_w{800.f};
  constexpr float k_win_h{600.f};

  // At zoom 2, the effective viewport is 400x300 — small enough that the player,
  // placed at the world's far edge, should pin the camera against that edge rather
  // than the zoom-1 clamp bound.
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 2.f};
  camera.follow_player(map.world_bounds.width_px, map.world_bounds.height_px, map, k_win_w, k_win_h);

  const float effective_w{k_win_w / camera.zoom};
  const float effective_h{k_win_h / camera.zoom};
  CHECK(camera.x == doctest::Approx(map.world_bounds.width_px - effective_w));
  CHECK(camera.y == doctest::Approx(map.world_bounds.height_px - effective_h));
}

TEST_CASE("follow_player - at zoom 1, matches the un-zoomed clamp bound") {
  MapView map;
  map.world_bounds = {.width_px = 500.f, .height_px = 500.f};
  constexpr float k_win_w{800.f};
  constexpr float k_win_h{600.f};

  // Map smaller than the viewport centers the camera regardless of player position.
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 1.f};
  camera.follow_player(250.f, 250.f, map, k_win_w, k_win_h);

  CHECK(camera.x == doctest::Approx((map.world_bounds.width_px - k_win_w) * 0.5f));
  CHECK(camera.y == doctest::Approx((map.world_bounds.height_px - k_win_h) * 0.5f));
}

TEST_CASE("follow_player - dead zone never exceeds half the viewport on a narrow window") {
  MapView map;
  map.world_bounds = {.width_px = 1000.f, .height_px = 1000.f};
  constexpr float k_win_w{100.f}; // narrower than 2 * Camera::k_horizontal_margin
  constexpr float k_win_h{600.f};

  Camera camera{.x = 0.f, .y = 0.f, .zoom = 1.f};
  camera.follow_player(250.f, 250.f, map, k_win_w, k_win_h);

  // The player must stay inside the effective viewport rather than being pushed past an edge.
  CHECK(camera.x <= 250.f);
  CHECK(250.f <= camera.x + k_win_w);
}

TEST_CASE("center_on: centers the viewport on the target and clamps to world bounds") {
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 1.f};

  // Target comfortably inside a large world: viewport centers on it exactly.
  camera.center_on(500.f, 400.f, WorldBounds{.width_px = 2000.f, .height_px = 1600.f}, 800.f, 600.f);
  CHECK(camera.x == doctest::Approx(500.f - 400.f));
  CHECK(camera.y == doctest::Approx(400.f - 300.f));

  // Target near the origin: clamped to 0 rather than going negative.
  camera.center_on(10.f, 10.f, WorldBounds{.width_px = 2000.f, .height_px = 1600.f}, 800.f, 600.f);
  CHECK(camera.x == doctest::Approx(0.f));
  CHECK(camera.y == doctest::Approx(0.f));

  // Target near the far edge: clamped to world_extent - effective_extent.
  camera.center_on(1990.f, 1590.f, WorldBounds{.width_px = 2000.f, .height_px = 1600.f}, 800.f, 600.f);
  CHECK(camera.x == doctest::Approx(2000.f - 800.f));
  CHECK(camera.y == doctest::Approx(1600.f - 600.f));
}

TEST_CASE("center_on: world smaller than the viewport centers the world (no UB clamp)") {
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 1.f};

  camera.center_on(100.f, 100.f, WorldBounds{.width_px = 400.f, .height_px = 300.f}, 800.f, 600.f);
  CHECK(camera.x == doctest::Approx((400.f - 800.f) * 0.5f));
  CHECK(camera.y == doctest::Approx((300.f - 600.f) * 0.5f));
}

TEST_CASE("center_on: zoom shrinks the effective viewport") {
  Camera camera{.x = 0.f, .y = 0.f, .zoom = 2.f}; // effective viewport is 400x300

  camera.center_on(500.f, 400.f, WorldBounds{.width_px = 2000.f, .height_px = 1600.f}, 800.f, 600.f);
  CHECK(camera.x == doctest::Approx(500.f - 200.f));
  CHECK(camera.y == doctest::Approx(400.f - 150.f));
}
