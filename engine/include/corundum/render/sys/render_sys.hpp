#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/render/data/render_state.hpp>

#include <expected>
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

namespace corundum::render::sys {

  /** @brief Initialise render state (pre-reserves entity draw-list buffer).
   *  @param[out] state  Uninitialised render state.
   *  @return ok on success.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(data::RenderState &state);

  /** @brief Release any render resources held by the state. */
  void clean_up(data::RenderState &state) noexcept;

  /** @brief Per-frame no-op; rendering is driven by render(). */
  void update(data::RenderState &state, float dt) noexcept;

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

  /** @brief Load the world manifest and initial chunks for world mode.
   *
   * Loads the manifest JSON, streams the 3×3 chunk window around the centre,
   * uploads all tileset textures, rebuilds collision aggregates, and returns
   * the info the caller needs to spawn the game world.
   *
   *  @param[in,out] r      Renderer for chunk texture loading.
   *  @param[out]    state  Render state populated with world manifest + active chunks.
   *  @param[in]     cfg    Game config.
   *  @return WorldLoadInfo on success, or std::unexpected with an error message.
   *  @pre cfg.paths.world_manifest_path must be a valid manifest JSON file.
   *  @post state.mode == RenderMode::World and active_chunks is non-empty.
   */
  [[nodiscard]] std::expected<WorldLoadInfo, std::string>
  load_world(corundum::platform::Renderer &r, data::RenderState &state, const corundum::core::GameConfig &cfg);

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
   * @param[in]     alpha  Interpolation factor in [0,1] for render smoothing.
   * @param[in]     win_w  Live window width in screen pixels.
   * @param[in]     win_h  Live window height in screen pixels.
   */
  void render(corundum::platform::Renderer &r, data::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::world::Scene &scene, const corundum::gameplay::FlagStore &flags,
              float alpha, int win_w, int win_h);

  /** @brief Tile width in source pixels of the first tileset in the first active chunk.
   *  @param[in] state  Render state.
   *  @return Tile width in pixels, or 0 if no active chunks or tilesets.
   */
  [[nodiscard]] int first_chunk_tile_px(const data::RenderState &state) noexcept;

} // namespace corundum::render::sys
