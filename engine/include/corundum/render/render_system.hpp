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

/** Record the camera and every entity's tile position as the start of a fixed step.
 *
 *  The renderer blends from this snapshot to the current state by the loop's alpha. Call
 *  immediately before each fixed step, and after a scene replacement (frame_camera_on does
 *  this) so the next frame blends within the new scene rather than from the old one.
 */
void snapshot_previous_step(render::RenderState &state, const corundum::world::Scene &scene) noexcept;

/** Tile position of `e` to draw this frame.
 *
 *  Blends from `e`'s start-of-step snapshot to `current` by `alpha`; returns
 *  `current` unchanged when `e` has no snapshot (spawned since the step began, or its
 *  index was recycled).
 *  @param alpha Fraction of a fixed step elapsed since the last step, in [0, 1].
 */
[[nodiscard]] core::math::TilePos interpolated_tile_position(const render::RenderState &state,
                                                             corundum::entities::EntityId e,
                                                             core::math::TilePos current, float alpha) noexcept;

/** Render the world for one frame; the caller owns screen-space overlays.
 *  @param renderer       Renderer for all draw calls.
 *  @param state          Writes frame scratch and WorldArea::above_z_cache, and records
 *                        world_view. Never changes chunk residency.
 *  @param config         Game config.
 *  @param scene          Scene (camera, entities, dialogue mode).
 *  @param alpha          Interpolation factor in [0,1] for render smoothing.
 *  @param window_logical Live window size in logical points.
 *  @post The renderer's view is reset to screen space, ready for overlay drawing.
 *  @post state.world_view holds the view the world was drawn with.
 */
void render(corundum::platform::Renderer &renderer, render::RenderState &state,
            const corundum::core::GameConfig &config, const corundum::world::Scene &scene, float alpha,
            core::math::IntSize window_logical);

} // namespace corundum::render
