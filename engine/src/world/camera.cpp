// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/world/camera.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/world_bounds.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace corundum::world {

  namespace {

    /// Center offset that keeps a world smaller than the viewport centered on screen.
    float centered_offset(float world_extent, float effective_extent) {
      return (world_extent - effective_extent) * 0.5f;
    }

    /// Clamp one camera axis so the viewport stays within the world, following the
    /// player when it crosses the dead-zone margin. Centers the axis when the world
    /// is smaller than the effective viewport.
    float follow_axis(float player_pos, float camera_pos, float world_extent, float effective_extent,
                      float effective_margin) {
      if (world_extent <= effective_extent)
        return centered_offset(world_extent, effective_extent);

      // A margin larger than half the viewport would overlap the opposite edge; cap it
      // so a narrow window still pins the player to the viewport instead of past it.
      effective_margin = std::min(effective_margin, effective_extent * 0.5f);

      const float player_screen{player_pos - camera_pos};
      float out{camera_pos};
      if (player_screen < effective_margin)
        out = player_pos - effective_margin;
      else if (player_screen > effective_extent - effective_margin)
        out = player_pos - (effective_extent - effective_margin);
      return std::clamp(out, 0.f, world_extent - effective_extent);
    }

    /// Center one camera axis on @p target and clamp the viewport to the world,
    /// centering the axis when the world is smaller than the effective viewport.
    float center_axis(float target, float world_extent, float effective_extent) {
      if (world_extent <= effective_extent)
        return centered_offset(world_extent, effective_extent);
      return std::clamp(target - (effective_extent * 0.5f), 0.f, world_extent - effective_extent);
    }

  } // namespace

  void Camera::follow_player(float player_x, float player_y, const MapView &map, float win_w, float win_h) noexcept {
    assert(zoom > 0.f);

    // Zoomed in sees less of the map, so the viewport and margins scale by 1/zoom to
    // keep on-screen distances (edge dead zones) constant regardless of zoom level.
    const float effective_w{win_w / zoom};
    const float effective_h{win_h / zoom};
    const float effective_horizontal_margin{k_horizontal_margin / zoom};
    const float effective_vertical_margin{k_vertical_margin / zoom};

    x = follow_axis(player_x, x, map.world_w_px, effective_w, effective_horizontal_margin);
    y = follow_axis(player_y, y, map.world_h_px, effective_h, effective_vertical_margin);
  }

  void Camera::apply_zoom(float zoom_delta, float anchor_x, float anchor_y, float min_zoom, float max_zoom) noexcept {
    assert(zoom > 0.f);
    assert(min_zoom > 0.f);
    assert(min_zoom <= max_zoom);

    if (zoom_delta == 0.f)
      return;

    const float zoom_old{zoom};
    const float zoom_new{std::clamp(zoom_old * std::pow(k_zoom_base, zoom_delta), min_zoom, max_zoom)};
    if (zoom_new == zoom_old)
      return;

    // screen = (world - camera) * zoom — the closed form below keeps the world point
    // under the anchor fixed across the zoom change.
    x += anchor_x * ((1.f / zoom_old) - (1.f / zoom_new));
    y += anchor_y * ((1.f / zoom_old) - (1.f / zoom_new));
    zoom = zoom_new;
  }

  void Camera::center_on(float target_x, float target_y, WorldBounds bounds, float win_w, float win_h) noexcept {
    assert(zoom > 0.f);

    const float effective_w{win_w / zoom};
    const float effective_h{win_h / zoom};
    x = center_axis(target_x, bounds.width_px, effective_w);
    y = center_axis(target_y, bounds.height_px, effective_h);
  }

} // namespace corundum::world
