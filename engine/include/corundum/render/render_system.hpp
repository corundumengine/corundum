// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/render/render_state.hpp>

namespace corundum::platform {
  class Renderer;
}

namespace corundum::world {
  struct Scene;
}

namespace corundum::render {

  /** @brief Record the camera and every entity's tile position as the start of a fixed step.
   *
   *  The renderer blends from this snapshot to the current state by the loop's alpha. Call
   *  immediately before each fixed step, and after a scene replacement (frame_camera_on does
   *  this) so the next frame blends within the new scene rather than from the old one.
   */
  void snapshot_previous_step(render::RenderState &state, const corundum::world::Scene &scene) noexcept;

  /** @brief Tile position of @p e to draw this frame.
   *
   *  Blends from @p e's start-of-step snapshot to (@p col, @p row) by @p alpha; returns
   *  (@p col, @p row) unchanged when @p e has no snapshot (spawned since the step began, or its
   *  index was recycled).
   *  @param[in] alpha Fraction of a fixed step elapsed since the last step, in [0, 1].
   */
  [[nodiscard]] core::math::Vec2 interpolated_tile_position(const render::RenderState &state,
                                                            corundum::entities::EntityId e, float col, float row,
                                                            float alpha) noexcept;

  /** @brief Render the world for one frame; the caller owns screen-space overlays.
   *  @param[in,out] r       Renderer for all draw calls.
   *  @param[in,out] state   Render state (chunk streaming may modify active set).
   *  @param[in]     cfg     Game config.
   *  @param[in]     scene   Scene (camera, entities, dialogue mode).
   *  @param[in]     alpha   Interpolation factor in [0,1] for render smoothing.
   *  @param[in]     win_w   Live window width in screen pixels.
   *  @param[in]     win_h   Live window height in screen pixels.
   *  @post The renderer's view is reset to screen space, ready for overlay drawing.
   */
  void render(corundum::platform::Renderer &r, render::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::world::Scene &scene, float alpha, int win_w, int win_h);

} // namespace corundum::render
