#pragma once
#include <corundum/gameplay/world/camera.hpp>

namespace corundum::gameplay::world {
  struct MapView;
}

namespace corundum::gameplay::sys {

  /** @brief Update the camera position to follow a target with a dead zone.
   *
   * The camera only moves when the target crosses a configurable dead zone
   * relative to the viewport centre. This prevents micro-jitter from small
   * movements (idle animation, velocity rounding).
   *
   *  @param[out] camera      Camera state; x/y are updated when the target leaves the dead zone.
   *  @param[in]  player_x    Target X world position in px.
   *  @param[in]  player_y    Target Y world position in px.
   *  @param[in]  map         Map dimensions for viewport clamping (world_w_px, world_h_px).
   *  @param[in]  win_w       Viewport width in px.
   *  @param[in]  win_h       Viewport height in px.
   *  @pre camera.zoom > 0.
   *  @post Camera is clamped so the viewport does not extend beyond the map.
   *  @performance O(1). No heap allocation.
   */
  void follow_player(corundum::gameplay::world::Camera &camera, float player_x, float player_y,
                     const corundum::gameplay::world::MapView &map, float win_w, float win_h) noexcept;

  /** @brief Adjust Camera::zoom by @p zoom_delta, keeping the world point under
   *  (@p anchor_x, @p anchor_y) visually fixed.
   *
   * @details New zoom is `clamp(camera.zoom * pow(k_zoom_base, zoom_delta), min_zoom, max_zoom)`.
   * The anchor is whatever screen point should stay put: the mouse cursor for
   * scroll-wheel zoom, or the screen center for keyboard/gamepad zoom (which has
   * no cursor to aim with).
   *
   *  @param[in,out] camera     Camera state; x/y/zoom are updated.
   *  @param[in]     zoom_delta Signed zoom amount for this call; a no-op at 0.
   *  @param[in]     anchor_x   Screen-space X (window pixels) to keep fixed.
   *  @param[in]     anchor_y   Screen-space Y (window pixels) to keep fixed.
   *  @param[in]     min_zoom   Lower clamp bound (GameConfig::min_zoom).
   *  @param[in]     max_zoom   Upper clamp bound (GameConfig::max_zoom).
   *  @performance O(1). No heap allocation.
   */
  void apply_zoom(corundum::gameplay::world::Camera &camera, float zoom_delta, float anchor_x, float anchor_y,
                  float min_zoom, float max_zoom) noexcept;

} // namespace corundum::gameplay::sys
