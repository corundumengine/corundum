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
   *  @post Camera is clamped so the viewport does not extend beyond the map.
   *  @performance O(1). No heap allocation.
   */
  void follow_player(corundum::gameplay::world::Camera &camera, float player_x, float player_y,
                     const corundum::gameplay::world::MapView &map, float win_w, float win_h) noexcept;

} // namespace corundum::gameplay::sys
