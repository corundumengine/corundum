#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <string>

namespace corundum::debug {

  /**
   * @brief Immutable snapshot of engine subsystems consumed by the debug overlay.
   *
   * Decouples the debug HUD from the Engine struct so the header includes only
   * the concrete types it actually reads.
   */
  struct OverlayInput {
    const render::RenderState &render_state;
    const core::GameConfig &cfg;
    const world::Scene &scene;
    const core::time::LoopTimer &timer;
  };

  /**
   * @brief Owning type for the debug HUD overlay.
   *
   * Consolidates HUD scratch state (enabled flag, EMA-smoothed render FPS) and
   * all debug-rendering behavior (collision geometry, player feet marker, text
   * panel) into a single class with one entry point — render(). Replaces the
   * previous loose aggregate (HudState) + free functions (draw_collision /
   * draw_hud / draw_overlays) pattern where state and behavior lived in
   * separate declarations and a transient HudData DTO was passed between them.
   *
   * Lifecycle is RAII: no heap allocations, no OS handles, no resources to
   * release. Defaults to disabled; flip @c enabled to true before calling
   * @c render to actually draw.
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

    /** @brief EMA-smoothed render FPS, updated each frame render() runs. */
    float smoothed_fps = 0.f;

    /**
     * @brief Draw all debug visualizations for the current frame.
     *
     * Reads render state, scene data, and timing from @p input; draws collision
     * geometry and the player feet marker in world space, then the HUD text
     * panel in screen space. Updates @c smoothed_fps from the loop timer's
     * last frame dt regardless of whether anything visible was drawn — the EMA
     * still advances so toggling the overlay on later shows a near-instant
     * value rather than the initial 0 climbing from zero.
     *
     * @param[in,out] r     Active renderer between begin_frame/end_frame.
     * @param[in]     input Bundle of engine subsystems required by the overlay.
     * @pre begin_frame() must have been called before this method.
     * @post platform::Renderer is left in screen-space view.
     */
    void render(platform::Renderer &r, const OverlayInput &input) noexcept;

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
    void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                        world::tilemap::CollisionRectsView rects, world::tilemap::CollisionTrianglesView tris,
                        core::math::IsometricParams iso, float zoom) const noexcept;

    /** @brief Draw the player feet-marker diamond in world space.
     *
     *  No-op when the isometric params are zero (no tilemap yet) or the player
     *  entity is missing the Transform/Collision components needed to anchor
     *  the marker at its feet.
     */
    void draw_player_marker(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport, float zoom,
                            const render::RenderState &render, const entities::World &w, entities::EntityId player,
                            core::math::IsometricParams iso) const noexcept;

    /** @brief Draw the top-right HUD text panel (FPS, grid, velocity, camera, stats). */
    void draw_text_panel(platform::Renderer &r, const render::RenderState &render, const core::GameConfig &cfg,
                         const world::Scene &scene) const noexcept;
  };

} // namespace corundum::debug
