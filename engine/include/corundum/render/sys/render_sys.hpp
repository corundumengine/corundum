#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/render/data/render_state.hpp>

#include <expected>
#include <optional>
#include <string>

namespace corundum::platform {
  class Renderer;
}

namespace corundum::resources {
  class CharacterRegistry;
}

namespace corundum::gameplay::world {
  struct Scene;
}

namespace corundum::gameplay::quest {
  class Registry;
}

namespace corundum::render::sys {

  /** @brief Initialise render state (pre-reserves entity draw-list buffer).
   *  @param[out] state  Uninitialised render state.
   *  @return ok on success.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(data::RenderState &state);

  /** @brief Release any render resources held by the state. */
  void clean_up(data::RenderState &state) noexcept;

  /** @brief Snapshot current entity transforms and camera into the prev_* fields
   *  for render interpolation.
   *  @param[out] state  Render state whose prev_col/prev_row/prev_count and
   *                     prev_cam_x/prev_cam_y/prev_zoom are overwritten.
   *  @param[in]  scene  Scene providing the current transforms and camera.
   *  @note Call once per frame, before running the fixed-timestep updates.
   */
  void snapshot_prev_frame(data::RenderState &state, const corundum::gameplay::world::Scene &scene) noexcept;

  /** @brief Build the sprite-index lookup tables from the character registry.
   *  @param[in,out] r         Renderer for texture loading.
   *  @param[out]    state     Render state to populate.
   *  @param[in]     registry  Loaded character registry.
   *  @post state.sprite_index is populated and ready for render_entities().
   */
  void load_sprite_index(corundum::platform::Renderer &r, data::RenderState &state,
                         const corundum::resources::CharacterRegistry &registry);

  /** @brief Load a TrueType font into the renderer and register it.
   *  @param[in,out] r      Renderer for font atlas creation.
   *  @param[out]    state  Render state; state.font_id is set on success.
   *  @param[in]     path   Filesystem path to the .ttf file.
   *  @return The font ID on success, or std::unexpected with an error message.
   */
  [[nodiscard]] std::expected<uint32_t, std::string> load_font(corundum::platform::Renderer &r,
                                                               data::RenderState &state, const std::string &path);

  /** @brief Load shared UI textures (dialog box border, etc.).
   *  @param[in,out] r      Renderer for texture creation.
   *  @param[out]    state  Render state; dialog_box border is configured.
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> load_ui_assets(corundum::platform::Renderer &r,
                                                                data::RenderState &state);

  /** @brief Load a single tilemap for map mode.
   *  @param[in,out] r             Renderer for tileset texture loading.
   *  @param[out]    state         Render state to populate with map data.
   *  @param[in]     tilemap_path  Path to the tilemap JSON.
   *  @param[in]     cfg           Game config (tile_scale, etc.).
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> load_map(corundum::platform::Renderer &r, data::RenderState &state,
                                                          const std::string &tilemap_path,
                                                          const corundum::core::GameConfig &cfg);

  /** @brief Info returned by load_world so the caller can spawn the game world.
   *
   * Contains the isometric projection parameters and the spawn position
   * (world centre in world coordinates) that the caller should pass to
   * gameplay::world::spawn_world(). Separates render resource loading from
   * game entity creation per the layer dependency rules.
   */
  struct WorldLoadInfo {
    float half_tw;
    float half_th;
    float x_origin;
    corundum::core::math::Vec2 spawn_world_pos;
  };

  /** @brief Optional spawn parameters for a (re)load of the world in world mode.
   *
   *  Overrides the default manifest-centre spawn and derives the 3×3 streaming
   *  window centre from the requested spawn tile, so a return to a non-central
   *  overworld location spawns the player into a streamed chunk.
   */
  struct WorldLoadParams {
    std::optional<int> spawn_col; ///< Overworld tile column; defaults to manifest centre.
    std::optional<int> spawn_row; ///< Overworld tile row; defaults to manifest centre.
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
   *  @post state.mode == RenderMode::World and state.chunks is non-empty.
   */
  [[nodiscard]] std::expected<WorldLoadInfo, std::string> load_world(corundum::platform::Renderer &r,
                                                                     data::RenderState &state,
                                                                     const corundum::core::GameConfig &cfg,
                                                                     const WorldLoadParams &params = {});

  /** @brief Apply dialog style (colours, fonts, spacing) from game config.
   *  @param[out] state  Render state; dialog_box style is configured.
   *  @param[in]  cfg    Game config with dialogue_render settings.
   */
  void configure_dialog_style(data::RenderState &state, const corundum::core::GameConfig &cfg);

  /** @brief Render the entire visible frame.
   *  @param[in,out] r      Renderer for all draw calls.
   *  @param[in,out] state  Render state (chunk streaming may modify active set).
   *  @param[in]     cfg    Game config.
   * @param[in]     scene  Scene (camera, entities, dialogue mode).
   * @param[in]     flags  Persistent game flags for conditional dialogue rendering.
   * @param[in]     quests Loaded quest registry for quest-gated choice visibility in the dialog box.
   * @param[in]     alpha  Interpolation factor in [0,1] for render smoothing.
   * @param[in]     win_w  Live window width in screen pixels.
   * @param[in]     win_h  Live window height in screen pixels.
   */
  void render(corundum::platform::Renderer &r, data::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::world::Scene &scene, const corundum::gameplay::FlagStore &flags,
              const corundum::gameplay::quest::Registry *quests, float alpha, int win_w, int win_h);

  /** @brief Tile width in source pixels of the first tileset in the first active chunk.
   *  @param[in] state  Render state.
   *  @return Tile width in pixels, or 0 if no active chunks or tilesets.
   */
  [[nodiscard]] int first_chunk_tile_px(const data::RenderState &state) noexcept;

  /** @brief Load one pending chunk into the active chunk window (state.chunks).
   *
   * Removes and loads the first entry from the pending-chunks queue. Called
   * between frames so the I/O does not hitch the render pass. Returns true
   * if a chunk was loaded, false if the queue is empty.
   *
   * @param[in,out] r      Renderer for texture uploads.
   * @param[in,out] state  Render state with pending_chunks queue.
   * @param[in]     cfg    Game config for portal path resolution.
   * @return True if a chunk was loaded, false if nothing to do.
   */
  /// Elevation of the tile under (col_f, row_f); resolves world-mode chunk ownership as needed.
  /// Returns 0 if no tilemap is loaded there. Definition/full docs in render_sys.cpp.
  float elevation_under(const data::RenderState &state, float col_f, float row_f) noexcept;

  bool load_one_pending_chunk(corundum::platform::Renderer &r, data::RenderState &state,
                              const corundum::core::GameConfig &cfg);

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
  [[nodiscard]] float elevation_under(const data::RenderState &state, float col_f, float row_f) noexcept;

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
  void rebuild_collision(data::RenderState &state) noexcept;

} // namespace corundum::render::sys
