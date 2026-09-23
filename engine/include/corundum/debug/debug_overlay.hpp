// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>

namespace corundum::debug {

  /**
   * @brief Immutable snapshot of engine subsystems consumed by the debug overlay.
   *
   * Passes the engine subsystems the debug overlay reads without coupling the
   * overlay to the Engine struct.
   */
  struct OverlayInput {
    const render::RenderState *render_state = nullptr;
    const core::GameConfig *cfg = nullptr;
    const world::Scene *scene = nullptr;
    const core::time::LoopTimer *timer = nullptr;

    /** @brief True when this frame's fixed-step drain exhausted its budget and dropped queued time. */
    bool step_budget_exhausted = false;
  };

  /**
   * @brief Owning type for the debug HUD overlay.
   *
   * Consolidates HUD scratch state (enabled flag, EMA-smoothed render FPS, shed
   * frame count) and all debug-rendering behavior (collision geometry, player
   * feet marker, text panel) behind a single render() entry point.
   *
   * Lifecycle is RAII: the object owns no heap memory, OS handles, or other
   * resources to release. Defaults to disabled; call render() every frame — it
   * advances the FPS EMA unconditionally and draws only while @c enabled.
   *
   * @note Not thread-safe. Call from the game thread, once per frame between
   *       the renderer's begin_frame() and end_frame() calls.
   */
  class HudOverlay {
  public:
    HudOverlay() = default;
    HudOverlay(const HudOverlay &) = delete;
    HudOverlay &operator=(const HudOverlay &) = delete;
    HudOverlay(HudOverlay &&) noexcept = default;
    HudOverlay &operator=(HudOverlay &&) noexcept = default;
    ~HudOverlay() = default;

    /** @brief When true, render() draws the debug overlay. */
    bool enabled = false;

    /** @brief EMA-smoothed render FPS, advanced on every render() call. */
    float smoothed_fps = 0.f;

    /** @brief Frames where the fixed-step drain exhausted its budget and shed queued simulation time.
     *
     *  Accumulates while the overlay is enabled. A steady climb means the simulation cannot keep up
     *  with the configured rate and is silently falling behind wall time.
     */
    uint32_t shed_frames = 0;

    /**
     * @brief Draw all debug visualizations for the current frame.
     *
     * Advances @c smoothed_fps from the loop timer's last frame dt on every
     * call, then draws collision geometry and the player feet marker in world
     * space and the HUD text panel in screen space while @c enabled. Advancing
     * the EMA even when disabled means toggling the overlay on later shows a
     * settled value instead of climbing from zero.
     *
     * @param[in,out] r     Active renderer between begin_frame/end_frame.
     * @param[in]     input Bundle of engine subsystems required by the overlay.
     * @pre begin_frame() must have been called before this method.
     * @post platform::Renderer is left in screen-space view; nothing is drawn and
     *       the renderer is untouched when @c enabled is false.
     */
    void render(platform::Renderer &r, const OverlayInput &input);

  private:
    /** @brief Resolve the isometric projection parameters from the active render mode.
     *
     *  Picks the single tilemap source per mode: the first active chunk's tilemap
     *  in World mode, the single loaded tilemap in SingleMap mode. Returns a
     *  zeroed IsometricParams when the render mode has no tilemap data yet
     *  (initial frames, or mode == None) — callers use this as a "no iso" sentinel.
     */
    [[nodiscard]] static core::math::IsometricParams resolve_isometric(const render::RenderState &render,
                                                                       const core::GameConfig &cfg) noexcept;

    /** @brief Draw the collision geometry (rects and triangles) in world space. */
    static void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                               world::tilemap::CollisionRectsView rects, world::tilemap::CollisionTrianglesView tris,
                               core::math::IsometricParams iso, float zoom) noexcept;

    /** @brief Draw the player feet-marker diamond in world space.
     *
     *  Marks the entity's anchor point — the tile centre the sprite, the footprint and the
     *  camera are all pinned to — at a fixed screen size, in its own colour. Unlike the
     *  footprint outline it shows a *point*, not an extent, and it still shows one when the
     *  player's footprint is degenerate; that is what makes it the tool for telling a
     *  misplaced footprint apart from a sprite drawn off its anchor.
     *
     *  No-op when the isometric params are zero (no tilemap yet) or the player
     *  entity is missing the Transform/Collision components needed to anchor
     *  the marker at its feet.
     */
    static void draw_player_marker(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                                   float zoom, const render::RenderState &render, const entities::World &w,
                                   entities::EntityId player, core::math::IsometricParams iso) noexcept;

    /** @brief Draw every entity's collision footprint in world space.
     *
     *  One outline per entity with a collision component, traced from the same rect the
     *  player-vs-NPC resolution consumes (see entities::footprint_of, the single definition
     *  of that convention) — so the outline shows exactly what blocks movement. The player's
     *  footprint uses the player colour; everything else uses the footprint colour.
     *  Entities with no transform row or no positive span are skipped.
     */
    static void draw_entity_footprints(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                                       float zoom, const render::RenderState &render, const entities::World &w,
                                       entities::EntityId player, core::math::IsometricParams iso) noexcept;

    /** @brief Draw the top-right HUD text panel (FPS, grid, speed, camera, stats). */
    void draw_text_panel(platform::Renderer &r, const render::RenderState &render, const core::GameConfig &cfg,
                         const world::Scene &scene) const;
  };

} // namespace corundum::debug
