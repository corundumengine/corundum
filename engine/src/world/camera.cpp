// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/world/camera.hpp>
#include <corundum/world/map_view.hpp>

#include <algorithm>
#include <cmath>

namespace corundum::world {

  namespace {

    /// Clamp one camera axis so the viewport stays within the world, following the
    /// player when it crosses the dead-zone margin. Centers the axis when the world
    /// is smaller than the effective viewport.
    float follow_axis(float player_pos, float camera_pos, float world_extent, float eff_extent, float eff_margin) {
      if (world_extent <= eff_extent)
        return (world_extent - eff_extent) * 0.5f;
      const float player_screen = player_pos - camera_pos;
      float out = camera_pos;
      if (player_screen < eff_margin)
        out = player_pos - eff_margin;
      else if (player_screen > eff_extent - eff_margin)
        out = player_pos - (eff_extent - eff_margin);
      return std::clamp(out, 0.f, world_extent - eff_extent);
    }

  } // namespace

  void Camera::follow_player(float player_x, float player_y, const MapView &map, float win_w, float win_h) noexcept {
    // Zoomed in sees less of the map, so the viewport and margins scale by 1/zoom to
    // keep on-screen distances (edge dead zones) constant regardless of zoom level.
    const float eff_w = win_w / zoom;
    const float eff_h = win_h / zoom;
    const float eff_horiz_margin = k_horiz_margin / zoom;
    const float eff_vert_margin = k_vert_margin / zoom;

    x = follow_axis(player_x, x, map.world_w_px, eff_w, eff_horiz_margin);
    y = follow_axis(player_y, y, map.world_h_px, eff_h, eff_vert_margin);
  }

  void Camera::apply_zoom(float zoom_delta, float anchor_x, float anchor_y, float min_zoom, float max_zoom) noexcept {
    if (zoom_delta == 0.f)
      return;

    const float zoom_old = zoom;
    const float zoom_new = std::clamp(zoom_old * std::pow(k_zoom_base, zoom_delta), min_zoom, max_zoom);
    if (zoom_new == zoom_old)
      return;

    // screen = (world - camera) * zoom — the closed form below keeps the world point
    // under the anchor fixed across the zoom change.
    x += anchor_x * ((1.f / zoom_old) - (1.f / zoom_new));
    y += anchor_y * ((1.f / zoom_old) - (1.f / zoom_new));
    zoom = zoom_new;
  }

  void Camera::center_on(float target_x, float target_y, float world_w, float world_h, float win_w,
                         float win_h) noexcept {
    const float eff_w = win_w / zoom;
    const float eff_h = win_h / zoom;
    x = (world_w <= eff_w) ? (world_w - eff_w) * 0.5f : std::clamp(target_x - (eff_w * 0.5f), 0.f, world_w - eff_w);
    y = (world_h <= eff_h) ? (world_h - eff_h) * 0.5f : std::clamp(target_y - (eff_h * 0.5f), 0.f, world_h - eff_h);
  }

} // namespace corundum::world
