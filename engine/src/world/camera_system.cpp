#include <corundum/world/camera_system.hpp>
#include <corundum/world/update.hpp>

#include <algorithm>
#include <cmath>

namespace corundum::world {

  void follow_player(corundum::world::Camera &camera, float player_x, float player_y,
                     const corundum::world::MapView &map, float win_w, float win_h) noexcept {
    constexpr float k_horiz_margin = 200.0f;
    constexpr float k_vert_margin = 150.0f;

    // The effective (world-space) viewport shrinks as zoom increases — zoomed in sees
    // less of the map, so the camera can pan further before hitting the map edge.
    // Margins are scaled the same way so they stay a constant on-screen distance from
    // the edge regardless of zoom level.
    const float zoom = camera.zoom;
    const float eff_w = win_w / zoom;
    const float eff_h = win_h / zoom;
    const float eff_horiz_margin = k_horiz_margin / zoom;
    const float eff_vert_margin = k_vert_margin / zoom;

    if (map.world_w_px <= eff_w) {
      camera.x = (map.world_w_px - eff_w) * 0.5f;
    } else {
      const float player_screen_x = player_x - camera.x;
      if (player_screen_x < eff_horiz_margin)
        camera.x = player_x - eff_horiz_margin;
      else if (player_screen_x > eff_w - eff_horiz_margin)
        camera.x = player_x - (eff_w - eff_horiz_margin);
      camera.x = std::clamp(camera.x, 0.f, map.world_w_px - eff_w);
    }

    if (map.world_h_px <= eff_h) {
      camera.y = (map.world_h_px - eff_h) * 0.5f;
    } else {
      const float player_screen_y = player_y - camera.y;
      if (player_screen_y < eff_vert_margin)
        camera.y = player_y - eff_vert_margin;
      else if (player_screen_y > eff_h - eff_vert_margin)
        camera.y = player_y - (eff_h - eff_vert_margin);
      camera.y = std::clamp(camera.y, 0.f, map.world_h_px - eff_h);
    }
  }

  void apply_zoom(corundum::world::Camera &camera, float zoom_delta, float anchor_x, float anchor_y, float min_zoom,
                  float max_zoom) noexcept {
    if (zoom_delta == 0.f)
      return;

    constexpr float k_zoom_base = 1.1f;

    const float zoom_old = camera.zoom;
    const float zoom_new =
        std::clamp(zoom_old * static_cast<float>(std::pow(k_zoom_base, zoom_delta)), min_zoom, max_zoom);
    if (zoom_new == zoom_old)
      return;

    // Keep (anchor_x, anchor_y) visually fixed: screen = (world - camera) * zoom, so
    // world = camera + anchor/zoom; solving for the new camera that keeps world (and
    // the anchor) constant across the zoom change gives this closed-form adjustment.
    camera.x += anchor_x * (1.f / zoom_old - 1.f / zoom_new);
    camera.y += anchor_y * (1.f / zoom_old - 1.f / zoom_new);
    camera.zoom = zoom_new;
  }

  void center_on(corundum::world::Camera &camera, float target_x, float target_y, float world_w, float world_h,
                 float win_w, float win_h) noexcept {
    const float eff_w = win_w / camera.zoom;
    const float eff_h = win_h / camera.zoom;
    camera.x =
        (world_w <= eff_w) ? (world_w - eff_w) * 0.5f : std::clamp(target_x - eff_w * 0.5f, 0.f, world_w - eff_w);
    camera.y =
        (world_h <= eff_h) ? (world_h - eff_h) * 0.5f : std::clamp(target_y - eff_h * 0.5f, 0.f, world_h - eff_h);
  }

} // namespace corundum::world
