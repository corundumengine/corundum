// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/render/render_state.hpp>

#include <expected>
#include <optional>
#include <string>

namespace corundum::core {
  class ContentSource;
}

namespace corundum::platform {
  class Renderer;
}

namespace corundum::world {
  struct Scene;
}

/// Area residency: the only functions that load, stream or unload the area's tilemaps and
/// textures (AGENTS.md, "The render layer owns world data").
namespace corundum::render {

  /** @brief Unload the current area: release its textures, clear every world field and return to
   *  RenderMode::None. Process-lifetime state (fonts, sprite_index, panel_skin, ui_scale,
   *  text_speed) is kept. Purges unreferenced textures, so call it between frames.
   *
   *  @post state.mode is RenderMode::None and state.area_texture_ids is empty.
   */
  void unload_world(corundum::platform::Renderer &r, render::RenderState &state);

  /** @brief Load a single tilemap for map mode.
   *  @param[in,out] r             Renderer for tileset texture loading.
   *  @param[out]    state         Render state to populate with map data.
   *  @param[in]     tilemap_path  Path to the tilemap JSON.
   *  @param[in]     cfg           Game config (tile_scale, etc.).
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> load_map(const corundum::core::ContentSource &content,
                                                          corundum::platform::Renderer &r, render::RenderState &state,
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
  [[nodiscard]] std::expected<WorldLoadInfo, std::string>
  load_world(const corundum::core::ContentSource &content, corundum::platform::Renderer &r, render::RenderState &state,
             const corundum::core::GameConfig &cfg, const WorldLoadParams &params = {});

  /** @brief Advance World-mode chunk streaming by one frame.
   *
   * Recenters the chunk window on the player, drops chunks and pending loads the window has
   * left, queues newly needed chunks, loads at most one pending chunk, and rebuilds the world
   * aggregates (collision, walkability, portals) once if the active set changed. This is the only
   * place world residency changes after load_world(); call it once per frame before the fixed
   * steps, so the simulation always reads aggregates that match the resident chunks. No-op
   * outside World mode or before any chunk is resident.
   */
  void stream_world_chunks(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                           render::RenderState &state, const corundum::core::GameConfig &cfg,
                           const corundum::world::Scene &scene);

} // namespace corundum::render
