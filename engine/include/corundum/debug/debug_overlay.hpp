#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/data/render_state.hpp>

#include <cstdint>

namespace corundum::debug {

  /**
   * @brief Immutable snapshot of engine subsystems consumed by the debug overlay.
   *
   * Decouples the debug HUD from the Engine struct so the header includes only
   * the concrete types it actually reads.
   */
  struct OverlayInput {
    const render::data::RenderState &render_state;
    const core::GameConfig &cfg;
    const gameplay::world::Scene &scene;
    const core::time::LoopTimer &timer;
  };

  /**
   * @brief Snapshot of runtime data rendered by the debug HUD overlay.
   */
  struct HudData {
    /** @brief Loaded font glyph handle. */
    uint32_t font_id{};
    /** @brief Window width in pixels, used to position the HUD on the right edge. */
    float win_w{};

    /** @brief EMA-smoothed render FPS. */
    float render_fps{};
    /** @brief Configured simulation FPS (target). */
    float sim_fps{};

    /** @brief Player tile-grid column. */
    float player_col{};
    /** @brief Player tile-grid row. */
    float player_row{};
    /** @brief Player column velocity (tiles/s). */
    float player_dc{};
    /** @brief Player row velocity (tiles/s). */
    float player_dr{};
    /** @brief Player facing direction. */
    gameplay::component::FacingDir player_facing{gameplay::component::FacingDir::South};
    /** @brief True if the player entity has a Facing component. */
    bool player_has_facing{};

    /** @brief Camera world-space X position. */
    float camera_x{};
    /** @brief Camera world-space Y position. */
    float camera_y{};
    /** @brief Number of active world chunks. */
    int active_chunks{};
    /** @brief Number of rectangle collision shapes. */
    int collision_rects{};
    /** @brief Number of diagonal triangle collision shapes. */
    int collision_tris{};
    /** @brief Number of alive entities. */
    int entity_count{};
  };

  /**
   * @brief Isometric projection parameters used for world-space debug overlays.
   */
  struct IsoParams {
    /** @brief Half the scaled diamond width for iso conversion. */
    float half_tw{};
    /** @brief Half the scaled diamond height for iso conversion. */
    float half_th{};
    /** @brief Isometric x-origin shift. */
    float x_origin{};
  };

  /**
   * @brief Draw collision geometry in world space as semi-transparent overlays.
   *
   * Converts Cartesian collision rects/triangles to isometric world space
   * before drawing so the overlay aligns with the rendered tilemap.
   * Switches to world-space view, draws rects and triangle AABBs, then resets
   * to screen-space view. Call between render() and end_frame().
   *
   * @param[in,out] r        Active renderer; must be between begin_frame/end_frame.
   * @param[in]     camera   Top-left world-pixel coordinate of the viewport.
   * @param[in]     viewport Viewport dimensions in pixels.
   * @param[in]     rects    Collision rectangles in Cartesian (Tiled pixel) space.
   * @param[in]     tris     Diagonal collision triangles in Cartesian (Tiled pixel) space.
   * @param[in]     iso      Isometric projection parameters; ignored when half_tw or half_th is zero.
   * @pre begin_frame() must have been called before this function.
   * @post platform::Renderer is left in screen-space view.
   */
  void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                      gameplay::world::tilemap::CollisionRectsView rects,
                      gameplay::world::tilemap::CollisionTrianglesView tris, IsoParams iso) noexcept;

  /**
   * @brief Draw the debug HUD text overlay in the top-left corner of the screen.
   *
   * @param[in,out] r    Active renderer in screen-space view.
   * @param[in]     data Snapshot of frame and world state to display.
   * @pre platform::Renderer must be in screen-space view (reset_screen_view() already called).
   * @pre data.font_id must be a valid loaded font handle.
   * @post platform::Renderer is left in screen-space view.
   */
  void draw_hud(platform::Renderer &r, const HudData &data) noexcept;

  /**
   * @brief Gather runtime state and draw all debug overlays.
   *
   * Reads render state, ECS world, camera, and timing data from @p input,
   * then renders collision geometry and the debug HUD text panel. Call
   * once per frame when the debug overlay is active.
   *
   * @param[in,out] r          Active renderer between begin_frame/end_frame.
   * @param[in]     input      Engine subsystems required by the debug overlay.
   * @param[in,out] smoothed_fps  EMA-smoothed render FPS (read and updated each frame).
   * @pre begin_frame() must have been called before this function.
   * @post platform::Renderer is left in screen-space view.
   */
  void draw_overlays(platform::Renderer &r, const OverlayInput &input, float &smoothed_fps) noexcept;

} // namespace corundum::debug
