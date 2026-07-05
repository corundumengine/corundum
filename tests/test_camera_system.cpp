#include <doctest/doctest.h>

#include <corundum/gameplay/sys/camera_system.hpp>
#include <corundum/gameplay/world/update.hpp>

using corundum::gameplay::sys::apply_zoom;
using corundum::gameplay::sys::follow_player;
using corundum::gameplay::world::Camera;
using corundum::gameplay::world::MapView;

namespace {

  constexpr float k_min_zoom = 0.5f;
  constexpr float k_max_zoom = 3.f;

  // World-space point currently under (anchor_x, anchor_y) at the camera's current zoom.
  float world_under_anchor(float camera_pos, float anchor, float zoom) {
    return camera_pos + anchor / zoom;
  }

} // namespace

TEST_CASE("apply_zoom — no-op when zoom_delta is zero") {
  Camera camera{10.f, 20.f, 1.5f};
  apply_zoom(camera, 0.f, 100.f, 50.f, k_min_zoom, k_max_zoom);
  CHECK(camera.x == doctest::Approx(10.f));
  CHECK(camera.y == doctest::Approx(20.f));
  CHECK(camera.zoom == doctest::Approx(1.5f));
}

TEST_CASE("apply_zoom — keeps the mouse-cursor anchor's world point fixed") {
  Camera camera{5.f, -3.f, 1.f};
  constexpr float anchor_x = 400.f;
  constexpr float anchor_y = 250.f;

  const float world_x_before = world_under_anchor(camera.x, anchor_x, camera.zoom);
  const float world_y_before = world_under_anchor(camera.y, anchor_y, camera.zoom);

  apply_zoom(camera, 1.f, anchor_x, anchor_y, k_min_zoom, k_max_zoom);

  CHECK(camera.zoom > 1.f);
  const float world_x_after = world_under_anchor(camera.x, anchor_x, camera.zoom);
  const float world_y_after = world_under_anchor(camera.y, anchor_y, camera.zoom);
  CHECK(world_x_after == doctest::Approx(world_x_before));
  CHECK(world_y_after == doctest::Approx(world_y_before));
}

TEST_CASE("apply_zoom — keeps the screen-center anchor's world point fixed on zoom out") {
  Camera camera{100.f, 60.f, 2.f};
  constexpr float anchor_x = 960.f;
  constexpr float anchor_y = 540.f;

  const float world_x_before = world_under_anchor(camera.x, anchor_x, camera.zoom);
  const float world_y_before = world_under_anchor(camera.y, anchor_y, camera.zoom);

  apply_zoom(camera, -1.f, anchor_x, anchor_y, k_min_zoom, k_max_zoom);

  CHECK(camera.zoom < 2.f);
  const float world_x_after = world_under_anchor(camera.x, anchor_x, camera.zoom);
  const float world_y_after = world_under_anchor(camera.y, anchor_y, camera.zoom);
  CHECK(world_x_after == doctest::Approx(world_x_before));
  CHECK(world_y_after == doctest::Approx(world_y_before));
}

TEST_CASE("apply_zoom — clamps to max_zoom") {
  Camera camera{0.f, 0.f, 2.9f};
  apply_zoom(camera, 5.f, 0.f, 0.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_max_zoom));
}

TEST_CASE("apply_zoom — clamps to min_zoom") {
  Camera camera{0.f, 0.f, 0.55f};
  apply_zoom(camera, -5.f, 0.f, 0.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_min_zoom));
}

TEST_CASE("apply_zoom — already-clamped zoom stays put and camera position is untouched") {
  Camera camera{7.f, 9.f, k_max_zoom};
  apply_zoom(camera, 1.f, 100.f, 100.f, k_min_zoom, k_max_zoom);
  CHECK(camera.zoom == doctest::Approx(k_max_zoom));
  CHECK(camera.x == doctest::Approx(7.f));
  CHECK(camera.y == doctest::Approx(9.f));
}

TEST_CASE("follow_player — zoomed-in viewport clamps to a smaller effective world extent") {
  MapView map;
  map.world_w_px = 2000.f;
  map.world_h_px = 2000.f;
  constexpr float win_w = 800.f;
  constexpr float win_h = 600.f;

  // At zoom 2, the effective viewport is 400x300 — small enough that the player,
  // placed at the world's far edge, should pin the camera against that edge rather
  // than the zoom-1 clamp bound.
  Camera camera{0.f, 0.f, 2.f};
  follow_player(camera, map.world_w_px, map.world_h_px, map, win_w, win_h);

  const float eff_w = win_w / camera.zoom;
  const float eff_h = win_h / camera.zoom;
  CHECK(camera.x == doctest::Approx(map.world_w_px - eff_w));
  CHECK(camera.y == doctest::Approx(map.world_h_px - eff_h));
}

TEST_CASE("follow_player — at zoom 1, matches the un-zoomed clamp bound") {
  MapView map;
  map.world_w_px = 500.f;
  map.world_h_px = 500.f;
  constexpr float win_w = 800.f;
  constexpr float win_h = 600.f;

  // Map smaller than the viewport centers the camera regardless of player position.
  Camera camera{0.f, 0.f, 1.f};
  follow_player(camera, 250.f, 250.f, map, win_w, win_h);

  CHECK(camera.x == doctest::Approx((map.world_w_px - win_w) * 0.5f));
  CHECK(camera.y == doctest::Approx((map.world_h_px - win_h) * 0.5f));
}
