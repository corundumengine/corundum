// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/render/render_state.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::platform {
  class Renderer;
}

namespace corundum::sprites {
  class CharacterRegistry;
}

namespace corundum::world {
  struct Scene;
}

namespace corundum::quest {
  class Registry;
}

namespace corundum::item {
  class Registry;
}

namespace corundum::render {

  /** @brief Initialise render state (pre-reserves entity draw-list buffer).
   *  @param[out] state  Uninitialised render state.
   *  @return ok on success.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(render::RenderState &state);

  /** @brief Release any render resources held by the state. */
  void clean_up(render::RenderState &state) noexcept;

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

  /** @brief Build the sprite-index lookup tables from the character registry.
   *  @param[in,out] r         Renderer for texture loading.
   *  @param[out]    state     Render state to populate.
   *  @param[in]     registry  Loaded character registry.
   *  @post state.sprite_index is populated and ready for render_entities().
   */
  void load_sprite_index(corundum::platform::Renderer &r, render::RenderState &state,
                         const corundum::sprites::CharacterRegistry &registry);

  /** @brief Load a TrueType font into the renderer and register it.
   *  @param[in,out] r      Renderer for font atlas creation.
   *  @param[out]    state  Render state; state.font_id is set on success.
   *  @param[in]     path   Filesystem path to the .ttf file.
   *  @return The font ID on success, or std::unexpected with an error message.
   */
  [[nodiscard]] std::expected<uint32_t, std::string> load_font(corundum::platform::Renderer &r,
                                                               render::RenderState &state, const std::string &path);

  /** @brief Load shared UI textures (dialog box border, etc.).
   *  @param[in,out] r     Renderer for texture creation.
   *  @param[out]    state Render state; dialog_box border is configured.
   *  @param[in]     path  Borders manifest to read; the border cell size is taken from the
   *                       upper_left frame, so multi-frame cells are supported.
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string>
  load_ui_assets(corundum::platform::Renderer &r, render::RenderState &state,
                 std::string_view path = "data/sprite_sheets/ui/borders.json");

  /** @brief Load a single tilemap for map mode.
   *  @param[in,out] r             Renderer for tileset texture loading.
   *  @param[out]    state         Render state to populate with map data.
   *  @param[in]     tilemap_path  Path to the tilemap JSON.
   *  @param[in]     cfg           Game config (tile_scale, etc.).
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> load_map(corundum::platform::Renderer &r, render::RenderState &state,
                                                          const std::string &tilemap_path,
                                                          const corundum::core::GameConfig &cfg);

  /** @brief Info returned by load_world so the caller can spawn the game world.
   *
   * Contains the isometric projection parameters and the spawn position
   * (world centre in world coordinates) that the caller should pass to
   * world::spawn_world(). Separates render resource loading from
   * game entity creation per the layer dependency rules.
   */
  struct WorldLoadInfo {
    float half_tw{};

    float half_th{};

    float x_origin{};

    corundum::core::math::Vec2 spawn_world_pos;
  };

  /** @brief Optional spawn parameters for a (re)load of the world in world mode.
   *
   *  Overrides the default manifest-centre spawn and derives the 3×3 streaming
   *  window centre from the requested spawn tile, so a return to a non-central
   *  overworld location spawns the player into a streamed chunk.
   */
  struct WorldLoadParams {
    std::optional<float> spawn_col; ///< Overworld tile column (may be fractional); defaults to manifest centre.

    std::optional<float> spawn_row; ///< Overworld tile row (may be fractional); defaults to manifest centre.
  };

  /** @brief Load the world manifest and initial chunks for world mode.
   *
   *  Loads the manifest JSON, streams the 3×3 chunk window around the centre,
   *  uploads all tileset textures, rebuilds collision aggregates, and returns
   *  the info the caller needs to spawn the game world.
   *
   *  @param[in,out] r       Renderer for chunk texture loading.
   *  @param[out]    state   Render state populated with world manifest + active chunks.
   *  @param[in]     cfg     Game config.
   *  @param[in]     params  Optional spawn tile; overrides the manifest-centre default
   *                         and re-centres the streaming window on that tile's chunk.
   *  @return WorldLoadInfo on success, or std::unexpected with an error message.
   *  @pre cfg.paths.world_manifest_path must be a valid manifest JSON file.
   *  @post state.mode == RenderMode::World and state.chunks is non-empty, or an error is returned.
   */
  [[nodiscard]] std::expected<WorldLoadInfo, std::string> load_world(corundum::platform::Renderer &r,
                                                                     render::RenderState &state,
                                                                     const corundum::core::GameConfig &cfg,
                                                                     const WorldLoadParams &params = {});

  /** @brief Apply dialog style (colours, fonts, spacing) from game config.
   *  @param[out] state  Render state; dialog_box style is configured.
   *  @param[in]  cfg    Game config with dialogue_render settings.
   */
  void configure_dialog_style(render::RenderState &state, const corundum::core::GameConfig &cfg);

  /** @brief Render the entire visible frame.
   *  @param[in,out] r      Renderer for all draw calls.
   *  @param[in,out] state  Render state (chunk streaming may modify active set).
   *  @param[in]     cfg    Game config.
   *  @param[in]     scene  Scene (camera, entities, dialogue mode).
   *  @param[in]     flags  Persistent game flags for conditional dialogue rendering.
   *  @param[in]     items  Loaded item registry for display names in the inventory panel; null hides it.
   *  @param[in]     alpha  Interpolation factor in [0,1] for render smoothing.
   *  @param[in]     win_w  Live window width in screen pixels.
   *  @param[in]     win_h  Live window height in screen pixels.
   */
  void render(corundum::platform::Renderer &r, render::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::world::Scene &scene, const corundum::world::FlagStore &flags,
              const corundum::item::Registry *items, float alpha, int win_w, int win_h);

  /** @brief Tile width in source pixels of the first tileset in the first active chunk.
   *  @param[in] state  Render state.
   *  @return Tile width in pixels, or 0 if no active chunks or tilesets.
   */
  [[nodiscard]] int first_chunk_tile_px(const render::RenderState &state) noexcept;

  /** @brief Advance World-mode chunk streaming by one frame.
   *
   * Recenters the chunk window on the player, drops chunks and pending loads the window has
   * left, queues newly needed chunks, loads at most one pending chunk, and rebuilds the world
   * aggregates (collision, walkability, portals) once if the active set changed. This is the only
   * place world residency changes after load_world(); call it once per frame before the fixed
   * steps, so the simulation always reads aggregates that match the resident chunks. No-op
   * outside World mode or before any chunk is resident.
   */
  void stream_world_chunks(corundum::platform::Renderer &r, render::RenderState &state,
                           const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene);

  /** @brief Elevation of the tile under (col_f, row_f), resolving chunk ownership in world mode.
   *
   * In world mode looks up the owning chunk via the active-chunk window and returns
   * discrete elevation. In single-map mode delegates to interpolated_elevation_at().
   * Returns 0 if no tilemap is loaded at the given coordinates.
   *
   * @param[in] state Render state carrying active-chunk data or single-tilemap data.
   * @param[in] col_f Fractional tile column.
   * @param[in] row_f Fractional tile row.
   * @return Tile elevation (≥0) at the queried position; 0 when out of bounds.
   */
  [[nodiscard]] float elevation_under(const render::RenderState &state, float col_f, float row_f) noexcept;

  /** @brief Rebuild the world-mode aggregated collision rects and triangles from active chunks.
   *
   * Clears and repopulates @c state.agg_collisions and @c state.agg_triangles by
   * walking @c state.chunks.active() and offsetting each chunk's tile-grid-local
   * collision geometry into world tile-grid coordinates (offsets are in tile units,
   * matching the tile-grid coordinate space the rest of the resolver operates in).
   *
   * @param[in,out] state  Render state in World mode whose aggregates will be overwritten.
   * @note No-op when the chunk window has no active chunks.
   */
  void rebuild_collision(render::RenderState &state);

  /** @brief Rebuild the world-mode aggregated walkability graph from the active chunks.
   *
   * Builds a WalkabilityGraph spanning the bounding box of the active chunk window,
   * indexed in global world tile coordinates (the graph's col_origin/row_origin record
   * the offset). Elevation-delta gating uses global lookups, so chunk seams gate
   * correctly. Clears the graph (no-op otherwise) when the chunk window is empty.
   *
   * @param[in,out] state           Render state in World mode.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_walkability(render::RenderState &state, int max_step_height);

  /** @brief Rebuild every world-mode aggregate for the active chunk window.
   *
   * Runs rebuild_collision(), rebuild_world_walkability() and the portal aggregation in one
   * step so the three stay in sync with the active chunk set. stream_world_chunks() and
   * load_world() call it after chunk mutations.
   *
   * @param[in,out] state           Render state in World mode.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_aggregates(render::RenderState &state, int max_step_height);

} // namespace corundum::render
